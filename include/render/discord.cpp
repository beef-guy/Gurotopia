#include "pch.hpp"

#include "tools/terminal.hpp"

#ifdef _WIN32
    #include <ws2tcpip.h>
    #include <wincrypt.h>
#else
    #include <unistd.h>
    #include <netdb.h>
    #include <sys/socket.h>
    #define SOCKET int
    #define INVALID_SOCKET (SOCKET)(~0)
    #define closesocket close
#endif

/* @note after wincrypt.h: OpenSSL undefines the names they share (X509_NAME...) */
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>

#include <fstream>
#include <random>

#include "discord.hpp"

std::string discord::webhook{};

void discord::load_webhook(const std::string &file)
{
    std::ifstream in(file);
    if (!in) return;
    std::string line{};
    std::getline(in, line);
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) line.pop_back();
    while (!line.empty() && line.front() == ' ') line.erase(0, 1);
    if (line.empty()) return;
    if (!line.starts_with("https://"))
    {
        print(std::format("{}: the webhook must start with https://", file));
        return;
    }
    webhook = line;
    print("/renderworld will post to Discord");
}

namespace
{
    std::string json_escape(const std::string &text)
    {
        std::string out{};
        for (const char c : text)
        {
            switch (c)
            {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': break;
                case '\t': out += "\\t"; break;
                default:
                    if (static_cast<u_char>(c) < 0x20) out += std::format("\\u{:04x}", static_cast<int>(c));
                    else out += c;
            }
        }
        return out;
    }

    /* @brief trust the same certificates the system does */
    void load_system_roots(SSL_CTX *ctx)
    {
#ifdef _WIN32
        X509_STORE *store = SSL_CTX_get_cert_store(ctx);
        HCERTSTORE system = CertOpenSystemStoreW(0, L"ROOT");
        if (system == nullptr) return;
        PCCERT_CONTEXT cert = nullptr;
        while ((cert = CertEnumCertificatesInStore(system, cert)) != nullptr)
        {
            const unsigned char *der = cert->pbCertEncoded;
            X509 *x509 = d2i_X509(nullptr, &der, static_cast<long>(cert->cbCertEncoded));
            if (x509 == nullptr) continue;
            X509_STORE_add_cert(store, x509);
            X509_free(x509);
        }
        CertCloseStore(system, 0);
#else
        SSL_CTX_set_default_verify_paths(ctx);
#endif
    }

    std::string ssl_error()
    {
        const unsigned long e = ERR_get_error();
        if (e == 0) return "connection closed";
        char buffer[256]{};
        ERR_error_string_n(e, buffer, sizeof(buffer));
        return buffer;
    }
}

std::string discord::post_image(const std::string &url, const std::string &message, const std::string &filename, const std::vector<u_char> &png)
{
    /* @note https://host/path */
    if (!url.starts_with("https://")) return "the webhook URL must start with https://";
    const std::size_t slash = url.find('/', 8);
    const std::string host = url.substr(8, slash == std::string::npos ? std::string::npos : slash - 8);
    const std::string path = slash == std::string::npos ? "/" : url.substr(slash);
    if (host.empty() || host.find_first_of(":@ ") != std::string::npos) return "the webhook URL looks wrong";

    addrinfo hints{}, *found = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), "443", &hints, &found) != 0 || found == nullptr) return std::format("could not find {}", host);

    SOCKET fd = INVALID_SOCKET;
    for (addrinfo *a = found; a != nullptr; a = a->ai_next)
    {
        fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd == INVALID_SOCKET) continue;
        if (connect(fd, a->ai_addr, static_cast<int>(a->ai_addrlen)) == 0) break;
        closesocket(fd);
        fd = INVALID_SOCKET;
    }
    freeaddrinfo(found);
    if (fd == INVALID_SOCKET) return std::format("could not connect to {}", host);
#ifdef _WIN32
    DWORD timeout = 20000;
#else
    timeval timeout{.tv_sec = 20, .tv_usec = 0};
#endif
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));

    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    if (ctx == nullptr) { closesocket(fd); return "OpenSSL could not start"; }
    load_system_roots(ctx);
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);

    SSL *ssl = SSL_new(ctx);
    std::string error{};
    std::string response{};
    if (ssl == nullptr) error = "OpenSSL could not start";
    else
    {
        SSL_set_fd(ssl, static_cast<int>(fd));
        SSL_set_tlsext_host_name(ssl, host.c_str()); // @note SNI
        SSL_set1_host(ssl, host.c_str()); // @note the certificate must be for this host
        if (SSL_connect(ssl) != 1)
        {
            const long verify = SSL_get_verify_result(ssl);
            error = (verify != X509_V_OK) ?
                std::format("{}'s certificate wasn't trusted ({})", host, X509_verify_cert_error_string(verify)) :
                std::format("secure connection failed: {}", ssl_error());
        }
        else
        {
            std::mt19937_64 rng{std::random_device{}()};
            const std::string boundary = std::format("----gurotopia{:016x}", rng());

            std::string head = std::format(
                "--{0}\r\n"
                "Content-Disposition: form-data; name=\"payload_json\"\r\n"
                "Content-Type: application/json\r\n\r\n"
                "{{\"content\":\"{1}\",\"allowed_mentions\":{{\"parse\":[]}},\"attachments\":[{{\"id\":0,\"filename\":\"{2}\"}}]}}\r\n"
                "--{0}\r\n"
                "Content-Disposition: form-data; name=\"files[0]\"; filename=\"{2}\"\r\n"
                "Content-Type: image/png\r\n\r\n",
                boundary, json_escape(message), json_escape(filename));
            const std::string tail = std::format("\r\n--{}--\r\n", boundary);

            const std::string request = std::format(
                "POST {} HTTP/1.1\r\n"
                "Host: {}\r\n"
                "User-Agent: Gurotopia (renderworld, 1.0)\r\n"
                "Content-Type: multipart/form-data; boundary={}\r\n"
                "Content-Length: {}\r\n"
                "Connection: close\r\n\r\n",
                path, host, boundary, head.size() + png.size() + tail.size());

            const auto send_all = [&](const void *data, std::size_t size) -> bool
            {
                const char *p = static_cast<const char*>(data);
                while (size > 0)
                {
                    const int n = SSL_write(ssl, p, static_cast<int>(std::min<std::size_t>(size, 1 << 16)));
                    if (n <= 0) return false;
                    p += n, size -= static_cast<std::size_t>(n);
                }
                return true;
            };
            if (!send_all(request.data(), request.size()) || !send_all(head.data(), head.size()) ||
                !send_all(png.data(), png.size()) || !send_all(tail.data(), tail.size()))
                error = std::format("sending failed: {}", ssl_error());
            else
            {
                char buffer[4096];
                int n = 0;
                while (response.size() < 65536 && (n = SSL_read(ssl, buffer, sizeof(buffer))) > 0) response.append(buffer, n);
                if (response.empty()) error = "Discord didn't answer";
            }
        }
        SSL_shutdown(ssl);
        SSL_free(ssl);
    }
    SSL_CTX_free(ctx);
    closesocket(fd);
    if (!error.empty()) return error;

    /* @note HTTP/1.1 200 OK (or 204) */
    const std::size_t space = response.find(' ');
    const int status = (space != std::string::npos) ? std::atoi(response.c_str() + space + 1) : 0;
    if (status >= 200 && status < 300) return {};

    std::string detail{};
    if (const std::size_t body = response.find("\r\n\r\n"); body != std::string::npos)
        detail = response.substr(body + 4, 200);
    if (status == 401 || status == 404) return std::format("Discord said the webhook doesn't exist (HTTP {}). copy the URL again", status);
    if (status == 413) return "the picture is too big for Discord";
    return std::format("Discord answered HTTP {} {}", status, detail);
}
