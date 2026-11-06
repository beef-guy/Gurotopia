#pragma once

/*
* @brief Steam Blocks like real Growtopia (action/steam.cpp):
*   - Steam Stomper: jump on it and steam shoots out below it. Steam Revolver: run through it and steam shoots upward.
*   - Steam Engine: feed it Nuclear Fuel (+100 degrees each). Above 500 degrees it makes steam to the left or right, the way it faces.
*   - steam runs through Steam Tubes, Pipes, Gears, Pistons, Bellows, Organs and Vents: straight when it can, else right, else left.
*     Valves send steam from above or below out of the bigger side and flip. Funnels have one way out. Crossovers are two pipes:
*     left-down and up-right. Cranks only pass steam their way (punch to turn them). Scramblers pick a random way out.
*     Collectors hold steam (up to 20 seconds) until they have enough to shoot out of every other side.
*   - Steam Doors open or shut, Steam Launchers charge up, Steam Lamps and Spikes only take steam from below,
*     Steam Vents get hot for 3 seconds, and a Spirit Storage Unit stores the Ghosts-In-A-Jar lying in front of it.
* the game moves its own copy of every puff with the same rules (it draws them). the server starts the steam with game packet 0x20
* (NET_GAME_PACKET_STEAM, byte 3 the kind, ints at 0x2c/0x30 the tile) and works out what the steam does here.
*/

constexpr short STEAM_STOMPER   = 3270;
constexpr short STEAM_REVOLVER  = 3496;
constexpr short STEAM_LAUNCHER  = 3498;
constexpr short STEAM_ENGINE_ID = 3558;
constexpr short NUCLEAR_FUEL    = 3196;

/* @brief the game says a player stepped on a Stomper, ran through a Revolver or bounced on a Launcher. @return true if it was one */
extern bool steam_tile_activate(ENetEvent &event, ::world &world, const ::pos &tile);

/* @brief Nuclear Fuel into a Steam Engine, wrenching an engine, punching a Steam Crank. @return true when nothing else should handle it */
extern bool steam_tile_change(ENetEvent &event, ::world &world, const ::gamePacket &gamePacket);

/* @brief a block was broken or placed here: an engine or collector there is gone */
extern void steam_tile_cleared(::world &world, const ::pos &pos);

/* @brief steam moves one tile every 100 ms, engines puff, heat wears off. call often */
extern void steam_tick();
