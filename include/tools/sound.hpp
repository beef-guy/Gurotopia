#pragma once

/* @brief sound effects. `file` is a path in the game folder, like "audio/door_open.wav" */

/* @brief only this player hears it */
extern void play_sfx(ENetPeer *peer, std::string_view file, int delay = 0);

/* @brief everyone in `world` hears it, coming from the player with `netid` */
extern void play_in_world(const std::string &world, int netid, std::string_view file, int delay = 0);
