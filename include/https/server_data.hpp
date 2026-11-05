#pragma once

class server_data 
{
public:
    std::string server{"127.0.0.1"};
    u_short port{17091};
    u_char type{1};
    u_char type2{1};
    std::string maint{"Server under maintenance. Please try again later."};
    std::string loginurl{"login-gurotopia.vercel.app"};
    std::string meta{"gurotopia"};

    /* @note launch options, see main.cpp --help */
    std::string config_file{"server_data.php"};
    int port_override{0};      // @note 0 = use the port in config_file
    u_short https_port{443};   // @note 0 = don't answer the client's HTTPS request (for a 2nd server on the same PC)
    std::size_t max_players{50ull};

    void init();
};
extern ::server_data gServer_data;
