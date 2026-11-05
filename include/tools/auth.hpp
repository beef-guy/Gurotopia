#pragma once
#ifndef AUTH_HPP
#define AUTH_HPP

    #include <string>

    namespace auth
    {
        /* @note returns "pbkdf2$<iterations>$<salt hex>$<hash hex>" */
        extern std::string hash_password(const std::string &password);

        /* @note checks a password against what is stored. old plain-text rows still work, so the caller can upgrade them. */
        extern bool verify_password(const std::string &password, const std::string &stored);
        extern bool is_hashed(const std::string &stored);

        /*
            one-time login ticket, handed out after a correct password and sent back by the client when it reconnects.
            without it anyone could reconnect with "user|<uid>" and become that account.
        */
        struct ticket
        {
            int token{};
            std::string uuid{};
        };
        extern ticket issue_ticket(int user_id);
        extern bool redeem_ticket(int user_id, int token, const std::string &uuid);

        /* @note limits wrong passwords per IP address */
        extern bool login_blocked(const ENetAddress &address);
        extern void login_failed(const ENetAddress &address);
        extern void login_succeeded(const ENetAddress &address);
    }

#endif
