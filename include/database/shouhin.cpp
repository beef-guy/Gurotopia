#include "pch.hpp"

#include <fstream>

#include "shouhin.hpp"

std::vector<std::pair<short, shouhin>> shouhin_tachi{};

void parse_store()
{
    std::ifstream file("resources/store.txt");
    for (std::string line; std::getline(file, line); )
    {
        if (line.empty() || line.starts_with("#")) continue; // @note '#' initiates a comment
        std::vector<std::string> pipes = readch(line, '|');
        if (pipes.size() < 8ull || pipes[5].empty() || pipes[6].empty()) // @note skip a bad line instead of crashing
        {
            print(std::format("resources/store.txt: skipped a bad line: {}", line));
            continue;
        }
        ::shouhin shouhin{
            .btn = pipes[1],
            .name = pipes[2],
            .rttx = pipes[3],
            .description = pipes[4],
            .tex1 = pipes[5].front(),
            .tex2 = pipes[6].front(), // @todo
            .cost = atoi(pipes[7].c_str())
        };
        if (pipes.size() > 8)
        {
            std::vector<std::string> tachi = readch(pipes[8], ',');
            for (std::string &item : tachi)
            {
                std::vector<std::string> co = readch(item, ':'); // @note 'co' short for colon
                if (co.size() < 2ull) continue;
                shouhin.items.emplace_back(atoi(co[0].c_str()), atoi(co[1].c_str()));
            }
        }
        shouhin_tachi.emplace_back(atoi(pipes[0].c_str()), shouhin);
    }
}