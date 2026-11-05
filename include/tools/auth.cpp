#include "pch.hpp"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <chrono>
#include <unordered_map>

#include "database/database.hpp"
#include "database/platform.hpp"

#include "auth.hpp"

namespace
{
    constexpr int PBKDF2_ITERATIONS = 100000;
    constexpr std::size_t SALT_SIZE = 16ull, HASH_SIZE = 32ull;

    constexpr auto TICKET_LIFETIME = std::chrono::seconds(60);

    constexpr int MAX_FAILED_LOGINS = 5;
    constexpr auto FAILED_LOGIN_WINDOW = std::chrono::minutes(5);

    using clock = std::chrono::steady_clock;

    std::string to_hex(const unsigned char *data, std::size_t size)
    {
        static constexpr char digits[] = "0123456789abcdef";
        std::string hex(size * 2, '0');
        for (std::size_t i = 0; i < size; ++i)
        {
            hex[i * 2]     = digits[data[i] >> 4];
            hex[i * 2 + 1] = digits[data[i] & 0x0f];
        }
        return hex;
    }

    bool from_hex(const std::string &hex, std::vector<unsigned char> &out)
    {
        if (hex.size() % 2 != 0) return false;
        out.resize(hex.size() / 2);
        for (std::size_t i = 0; i < out.size(); ++i)
        {
            int value = 0;
            for (int j = 0; j < 2; ++j)
            {
                const char c = hex[i * 2 + j];
                value <<= 4;
                if      (c >= '0' && c <= '9') value |= c - '0';
                else if (c >= 'a' && c <= 'f') value |= c - 'a' + 10;
                else return false;
            }
            out[i] = static_cast<unsigned char>(value);
        }
        return true;
    }

    bool random_bytes(unsigned char *data, int size)
    {
        return RAND_bytes(data, size) == 1;
    }

    bool pbkdf2(const std::string &password, const unsigned char *salt, std::size_t salt_size, int iterations, unsigned char *out)
    {
        return PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()), salt, static_cast<int>(salt_size),
                                 iterations, EVP_sha256(), static_cast<int>(HASH_SIZE), out) == 1;
    }

    struct pending_ticket
    {
        auth::ticket ticket{};
        clock::time_point expires{};
    };
    std::unordered_map<int, pending_ticket> tickets; // @note user_id -> ticket

    struct failures
    {
        int count{};
        clock::time_point first{};
    };
    std::unordered_map<std::uint32_t, failures> failed_logins; // @note IPv4 -> wrong passwords

    std::uint32_t ip_key(const ENetAddress &address)
    {
        std::uint32_t key{};
        std::memcpy(&key, address.host.v4, sizeof(key));
        return key;
    }
}

std::string auth::hash_password(const std::string &password)
{
    unsigned char salt[SALT_SIZE], hash[HASH_SIZE];
    if (!random_bytes(salt, sizeof(salt)) || !pbkdf2(password, salt, sizeof(salt), PBKDF2_ITERATIONS, hash))
        throw std::runtime_error("could not hash password");

    return std::format("pbkdf2${}${}${}", PBKDF2_ITERATIONS, to_hex(salt, sizeof(salt)), to_hex(hash, sizeof(hash)));
}

bool auth::is_hashed(const std::string &stored)
{
    return stored.starts_with("pbkdf2$");
}

bool auth::verify_password(const std::string &password, const std::string &stored)
{
    if (password.empty() || stored.empty()) return false;

    if (!is_hashed(stored)) // @note an account made before passwords were hashed
    {
        return stored.size() == password.size() &&
               CRYPTO_memcmp(stored.data(), password.data(), stored.size()) == 0;
    }

    const std::vector<std::string> parts = readch(stored, '$');
    if (parts.size() != 4ull) return false;

    int iterations = 0;
    try { iterations = std::stoi(parts[1ull]); }
    catch (const std::logic_error &) { return false; }
    if (iterations <= 0 || iterations > 10000000) return false;

    std::vector<unsigned char> salt, expected;
    if (!from_hex(parts[2ull], salt) || !from_hex(parts[3ull], expected) || expected.size() != HASH_SIZE) return false;

    unsigned char hash[HASH_SIZE];
    if (!pbkdf2(password, salt.data(), salt.size(), iterations, hash)) return false;

    return CRYPTO_memcmp(hash, expected.data(), HASH_SIZE) == 0;
}

namespace
{
    bool ticket_matches(const auth::ticket &expected, int token, const std::string &uuid)
    {
        const bool token_ok = (token != 0 && token == expected.token);
        const bool uuid_ok  = (uuid.size() == expected.uuid.size() && !uuid.empty() &&
                               CRYPTO_memcmp(uuid.data(), expected.uuid.data(), uuid.size()) == 0);
        return token_ok || uuid_ok;
    }

    void store_ticket_in_platform(const std::string &server, int user_id, const auth::ticket &ticket)
    {
        ::hStmt hStmt{ std::format(
            "REPLACE INTO `{}`.login_tickets (server_name, uid, token, uuid, expires_at) "
            "VALUES (?, ?, ?, ?, NOW() + INTERVAL {} SECOND)", platform::database, TICKET_LIFETIME.count()) };
        MYSQL_BIND params[4] = { make_bind_in(server), make_bind_in(user_id), make_bind_in(ticket.token), make_bind_in(ticket.uuid) };
        hStmt.bind_param(params);
        hStmt.execute();

        mysql_query(db.mysql, std::format("DELETE FROM `{}`.login_tickets WHERE expires_at < NOW()", platform::database).c_str());
    }

    bool redeem_platform_ticket(int user_id, int token, const std::string &uuid)
    {
        auth::ticket stored{};
        {
            ::hStmt hStmt{ std::format(
                "SELECT token, uuid FROM `{}`.login_tickets WHERE server_name = ? AND uid = ? AND expires_at >= NOW() LIMIT 1",
                platform::database) };
            MYSQL_BIND params[2] = { make_bind_in(platform::server_name), make_bind_in(user_id) };
            hStmt.bind_param(params);

            u_long length = 0;
            MYSQL_BIND results[2] = { make_bind_out(stored.token), make_bind_out(stored.uuid) };
            results[1].length = &length;
            if (hStmt.pStmt == nullptr || mysql_stmt_bind_result(hStmt.pStmt, results)) return false;

            hStmt.execute();
            if (mysql_stmt_fetch(hStmt.pStmt) != 0) return false; // @note no ticket for this player here
            stored.uuid.resize(length);
        }
        if (!ticket_matches(stored, token, uuid)) return false;

        ::hStmt hStmt{ std::format("DELETE FROM `{}`.login_tickets WHERE server_name = ? AND uid = ?", platform::database) };
        MYSQL_BIND params[2] = { make_bind_in(platform::server_name), make_bind_in(user_id) };
        hStmt.bind_param(params);
        hStmt.execute();
        return hStmt.pStmt != nullptr && mysql_stmt_affected_rows(hStmt.pStmt) == 1; // @note a ticket works once, even if two logins race
    }
}

auth::ticket auth::issue_ticket(int user_id, const std::string &target_server)
{
    const clock::time_point now = clock::now();
    std::erase_if(tickets, [now](const auto &pair) { return pair.second.expires < now; }); // @note forget expired tickets

    unsigned char raw[4 + 16];
    if (!random_bytes(raw, sizeof(raw))) throw std::runtime_error("could not make a login ticket");

    ::auth::ticket ticket{};
    std::memcpy(&ticket.token, raw, sizeof(int));
    ticket.token &= 0x7fffffff; // @note the client keeps it as a signed int
    if (ticket.token == 0) ticket.token = 1;
    ticket.uuid = to_hex(raw + 4, 16);

    if (platform::enabled() && !target_server.empty())
    {
        store_ticket_in_platform(target_server, user_id, ticket);
        return ticket;
    }
    tickets[user_id] = { ticket, now + TICKET_LIFETIME };
    return ticket;
}

bool auth::redeem_ticket(int user_id, int token, const std::string &uuid)
{
    if (platform::enabled() && !platform::server_name.empty() && redeem_platform_ticket(user_id, token, uuid)) return true;

    auto it = tickets.find(user_id);
    if (it == tickets.end()) return false;

    const pending_ticket &pending = it->second;
    if (pending.expires < clock::now())
    {
        tickets.erase(it);
        return false;
    }

    if (!ticket_matches(pending.ticket, token, uuid)) return false; // @note a wrong guess doesn't burn the real player's ticket. guessing is rate limited per IP.

    tickets.erase(it); // @note a ticket works once
    return true;
}

bool auth::login_blocked(const ENetAddress &address)
{
    auto it = failed_logins.find(ip_key(address));
    if (it == failed_logins.end()) return false;

    if (clock::now() - it->second.first > FAILED_LOGIN_WINDOW)
    {
        failed_logins.erase(it);
        return false;
    }
    return it->second.count >= MAX_FAILED_LOGINS;
}

void auth::login_failed(const ENetAddress &address)
{
    const clock::time_point now = clock::now();
    ::failures &entry = failed_logins[ip_key(address)];
    if (entry.count == 0 || now - entry.first > FAILED_LOGIN_WINDOW) entry = { 0, now };
    ++entry.count;
}

void auth::login_succeeded(const ENetAddress &address)
{
    failed_logins.erase(ip_key(address));
}
