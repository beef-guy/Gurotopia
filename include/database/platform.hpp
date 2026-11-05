#pragma once
#ifndef PLATFORM_HPP
#define PLATFORM_HPP

    #include <optional>
    #include <string>

    /*
        the platform database ties many servers together: which server names exist, which port each one is on,
        and the one-time login tickets the gateway hands to them. it's only used when --platform-db, --gateway or --name is given.
    */
    namespace platform
    {
        extern std::string database;    // @note e.g. gurotopia_platform
        extern std::string server_name; // @note this server's name (--name), empty for the gateway or a plain server
        extern bool gateway;            // @note --gateway: only handles logins and sends players on
        extern std::string default_server; // @note --default-server: where the gateway sends players who didn't type a server name

        extern bool enabled();
        extern bool valid_name(const std::string &name);

        extern void create_tables();

        struct server
        {
            std::string name{};
            int port{};
            std::string db_name{};
        };
        /* @note a server players can be sent to right now */
        extern std::optional<server> find_running(const std::string &name);

        /* @note this server's row: status 'running' or 'stopped', how many are online */
        extern void report(const std::string &status, int online);
    }

#endif
