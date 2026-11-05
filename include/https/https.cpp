#include "pch.hpp"

#include "tools/terminal.hpp"
#include <openssl/ssl.h>

#ifdef _WIN32
    #include <ws2tcpip.h>
#else
    #include <csignal>
    #include <unistd.h>
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h> // @note TCP_DEFER_ACCEPT
    #include <sys/socket.h>

    #define SOCKET int
    #define INVALID_SOCKET	(SOCKET)(~0)
    #define SOCKET_ERROR	(-1)
#endif

#include "server_data.hpp"

#include "https.hpp"

/* cross-platform socket close */
static void cross_close(SOCKET fd)
{
    int ret =
#ifdef _WIN32
    closesocket(fd)
#else // @note unix
    close(fd)
#endif
    ; // ending of ret. hehe some silly code ;)

    if (ret == SOCKET_ERROR) print("socket close error.");
}

/* cross-platform WSA error log, fallback on linux with strerror() */
static void cross_log(const std::string &message)
{
#ifdef _WIN32
    print(std::format("{}: {}", message, WSAGetLastError()));
#else // @note unix
    print(std::format("{}: {}", message, strerror(errno)));
#endif
}

void https::listener()
{
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
    constexpr int enable = 1;

    /* https://docs.openssl.org/3.0/man3/SSL_CTX_new/#return-values */
    SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx)
    {
        print("The creation of a new SSL_CTX object failed.");
    }
    /* https://docs.openssl.org/master/man3/SSL_CTX_use_certificate/#return-values */
    if (SSL_CTX_use_certificate_file(ctx, "resources/ctx/server.crt", SSL_FILETYPE_PEM) != 1 ||
        SSL_CTX_use_PrivateKey_file(ctx, "resources/ctx/server.key", SSL_FILETYPE_PEM)  != 1)
    {
        print("certificate could not be loaded.");
    }
    else {
        /* https://docs.openssl.org/master/man3/SSL_CTX_use_certificate/#return-values */
        X509 *cert = SSL_CTX_get0_certificate(ctx);
        if (!cert)
        {
            print("SSL_CTX_get0_certificate() failed.");
        }
        /* https://docs.openssl.org/3.2/man3/X509_get0_notBefore/#return-values */
        const ASN1_TIME *notAfter = X509_get0_notAfter(cert);
        if (notAfter == nullptr)
        {
            print("X509_get0_notAfter() failed.");
        }
        int day, sec;
        /* https://docs.openssl.org/1.0.2/man3/ASN1_TIME_set/#return-values */
        if (ASN1_TIME_diff(&day, &sec, nullptr, notAfter) == 0)
        {
            print("ASN1_TIME_diff() failed.");
        }
        print(std::format("certificate expires in {} days", day));
    }
    
#ifdef SIGPIPE // @note unix
    std::signal(SIGPIPE, SIG_IGN);
#endif

    /* https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-socket */
    SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket == INVALID_SOCKET)
    {
        cross_log("socket function failed");
    }

#ifdef SO_REUSEADDR
    setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, (char*)&enable, sizeof(enable));
#endif
#ifdef TCP_DEFER_ACCEPT // @note unix
    setsockopt(socket, IPPROTO_TCP, TCP_DEFER_ACCEPT, (char*)&enable, sizeof(enable));
#endif
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(443);
    socklen_t addrlen = sizeof(addr);

    /* https://learn.microsoft.com/en-us/windows/win32/api/winsock/nf-winsock-bind */
    if (bind(socket, (struct sockaddr*)&addr, addrlen) == SOCKET_ERROR)
    {
        cross_log("could not bind socket");
        print("port 443 is in use, this server won't answer logins. is GOWebServer still running?");
        cross_close(socket);
        return; // @note the game server keeps running, this avoids spinning on a dead socket
    }

    const std::string Content =
        std::format(
            "server|{}\n"
            "port|{}\n"
            "type|{}\n"
            "type2|{}\n" // @todo remove for older clients
            "#maint|{}\n"
            "loginurl|{}\n"
            "meta|{}\n"
            "RTENDMARKERBS1001", 
            gServer_data.server, gServer_data.port, gServer_data.type, gServer_data.type2, gServer_data.maint, gServer_data.loginurl, gServer_data.meta
        );
    const std::string response =
        std::format(
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: {}\r\n"
            "Connection: close\r\n\r\n"
            "{}",
            Content.size(), Content);

    /* https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-listen */
    if (listen(socket, SOMAXCONN) == SOCKET_ERROR)
    {
        cross_log("failed to listen on socket");
        cross_close(socket);
        return;
    }
    else print(std::format("listening on {}:{}", gServer_data.server, gServer_data.port));
    
    while (true)
    {
        /* https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-accept */
        SOCKET fd = accept(socket, reinterpret_cast<sockaddr*>(&addr), &addrlen);
        if (fd == INVALID_SOCKET) continue;

        /* @note one slow connection must not hold up everyone else's login */
#ifdef _WIN32
        const DWORD timeout = 3000; // @note milliseconds
#else
        const timeval timeout{ .tv_sec = 3, .tv_usec = 0 };
#endif
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));

        /* https://docs.openssl.org/3.0/man3/SSL_new/#return-values */
        SSL *ssl = SSL_new(ctx);
        if (!ssl)
        {
            cross_close(fd);
            continue;
        }
        /* https://docs.openssl.org/3.0/man3/SSL_set_fd/#return-values */
        /* https://docs.openssl.org/3.0/man3/SSL_accept/#return-values */
        else if (SSL_set_fd(ssl, fd) != 1 || SSL_accept(ssl) <= 0) {
            //int ret = SSL_get_error(ssl, 3);
            print("SSL handshake failed.");
        }
        else {
            char buf[245+38]; // @note size of growtopia's POST request.

            int rbytes = SSL_read(ssl, buf, sizeof(buf));
            if (rbytes <= 0) print("SSL_read() failed."); // @todo support retryable
            else {
                print(std::string(buf, rbytes)); // @note to confirm the peer connected. else you could also see if loginurl dashboard appears.

                int wbytes = SSL_write(ssl, response.c_str(), response.length());
                if (wbytes < response.length()) print("SSL_write() failed."); // @todo support retryable
            }
        }

        /* "It can also occur when not all data was read using SSL_read()." */
        if (SSL_is_init_finished(ssl) && SSL_shutdown(ssl) < 0)
        {
            //int ret = SSL_get_error(ssl, 3);
            print("SSL_shutdown() failed.");
        }
        SSL_free(ssl);
        if (::shutdown(fd, 2) == SOCKET_ERROR) cross_log("failed to shutdown socket"); // @todo unsure if WSA can handle this.
        cross_close(fd);
    }
}

#ifndef _WIN32
    #undef SOCKET
#endif
