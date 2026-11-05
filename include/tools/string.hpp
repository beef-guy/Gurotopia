#pragma once

/*
    @param str the whole string
    @param c the char to search for and split
    @return std::vector<std::string> storing each split off from c found in the str.
*/
extern std::vector<std::string> readch(const std::string &str, char deli) ;

struct hPipe
{
    hPipe(const std::string &str)
    {
        std::vector<std::string> pipes = readch(str, '|');
        for (std::size_t i = 0; i + 1 < pipes.size(); ++i)
            mPairs.emplace_back(pipes[i], pipes[i + 1]);
    }
    std::string operator[](const std::string &key) const 
    { 
        for (const std::pair<std::string, std::string> &pair : mPairs)
            if (pair.first == key) return pair.second;

        return "";
    }
private:
    std::vector<std::pair<std::string, std::string>> mPairs;
};

extern std::string join(const std::vector<std::string>& range, const std::string& del);

/*
    @return everything after the command, e.g. "/warp start" returns "start". empty if nothing was typed.
*/
inline std::string_view command_arg(std::string_view text)
{
    const std::size_t space = text.find(' ');
    return (space == std::string_view::npos) ? std::string_view{} : text.substr(space + 1);
}

/*
    @return true if string contains ONLY numbers or alphabet [1, 2, 3, a, b, c]
*/
extern bool isalnum(const std::string &str);

extern std::string base64_decode(const std::string& encoded);

/* 
    @return '1' (true) || '0' (false) 
*/
inline constexpr auto to_char = [](bool b) { return b ? '1' : '0'; };
