#include "pch.hpp"

#include <fstream>

#include "server_data.hpp"

::server_data gServer_data{};

void ::server_data::init()
{
    std::ifstream file("server_data.php");
    if (!file.is_open())
    {
        std::ofstream ostrm("server_data.php");
        ostrm << 
            std::format(
                "server|{}\n"
                "port|{}\n"
                "type|{}\n"
                "type2|{}\n"
                "#maint|{}\n"
                "loginurl|{}\n"
                "meta|{}\n"
                "RTENDMARKERBS1001", 
                this->server, this->port, this->type, this->type2, this->maint, this->loginurl, this->meta
            );
    } // @note close ostrm
    else
    {
        /* @note read each line by name, a missing or empty line keeps its default */
        for (std::string line; std::getline(file, line); )
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();

            const std::size_t pipe = line.find('|');
            if (pipe == std::string::npos || pipe + 1 == line.size()) continue;

            const std::string key = line.substr(0, pipe);
            const std::string value = line.substr(pipe + 1);

            if      (key == "server")   this->server = value;
            else if (key == "port")     this->port = static_cast<u_short>(atoi(value.c_str()));
            else if (key == "type")     this->type = static_cast<u_char>(atoi(value.c_str()));
            else if (key == "type2")    this->type2 = static_cast<u_char>(atoi(value.c_str()));
            else if (key == "#maint")   this->maint = value;
            else if (key == "loginurl") this->loginurl = value;
            else if (key == "meta")     this->meta = value;
        }
    }
} // @note close file
