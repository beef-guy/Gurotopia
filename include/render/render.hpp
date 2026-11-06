#pragma once

/* @brief /renderworld: draws a world to a PNG with the block textures from the Growtopia game folder */
namespace render
{
    struct tile
    {
        short fg{}, bg{};
        u_char state2{}, state3{}; // @note block::state[2] (S_LEFT...) and block::state[3] (water, fire, paint)
    };
    struct drop
    {
        u_short id{};
        float x{}, y{}; // @note pixels
    };
    /* @brief a copy of what a world looks like, so it can be drawn on another thread */
    struct snapshot
    {
        std::string name{};
        int width{100}, height{};
        std::vector<tile> tiles{};
        std::vector<drop> drops{};
    };

    /* @note the Growtopia "game" folder with the .rttex files. empty = %LOCALAPPDATA%\Growtopia\game (Windows) or ./game */
    extern std::string texture_folder;

    /* @note call on the main thread */
    extern snapshot take(const ::world &world);

    /*
    * @brief draw a world. safe to call on another thread (items must already be loaded)
    * @return the PNG file, or empty with `error` saying why
    */
    extern std::vector<u_char> draw(const snapshot &shot, std::string &error);
}
