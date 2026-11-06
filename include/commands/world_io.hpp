#pragma once

/*
* @brief /export and /import: copy worlds to and from .dat files in the worlds folder next to the server.
*   /export <world>          writes worlds/<WORLD>.dat (the world as the game client receives it)
*   /import                  lists the .dat files in worlds/
*   /import <file> <world>   builds a new world from worlds/<file>.dat
* @note the file is the client's world data (SEND_MAP_DATA). source: github.com/voidkle/gtps-guide guide/world-data.md, guide/tile-extras.md
*/
extern void export_command(ENetEvent &event, const std::string_view text);
extern void import_command(ENetEvent &event, const std::string_view text);
