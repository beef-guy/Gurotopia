#pragma once

/*
 * Server scripts: the server's owner writes Lua on the website (web/src/scripts.js), the game runs it here.
 *
 * Scripts are untrusted (anyone who makes a server writes them), so they run in a sandbox:
 * - no files, no os.execute, no loading other code or bytecode, no debug library
 * - a memory limit (--lua-memory) and a time limit for every event, timer and command
 * - things that could pull a world out from under the game's own code (warping, kicking) wait until
 *   the game is done with the packet that started the event
 *
 * Every event below does nothing when no script listens for it.
 */
#include <string>
#include <string_view>
#include <vector>

namespace lua
{
    extern std::size_t memory_limit_mb; // @note --lua-memory, per server (default 64)

    /* @note creates the tables, loads every enabled script. call once the database is connected */
    extern void init();
    /* @note the website saved a script: forget everything scripts made (timers, commands) and load them again */
    extern void reload();
    /* @note runs timers, waiting actions, saves script data and the log. call every loop */
    extern void tick();
    /* @note saves script data before the server stops */
    extern void shutdown();
    /* @note writes saved values and the log to the database now */
    extern void flush();

    /* @note a player logged in / is logging out */
    extern void player_login(ENetPeer *peer);
    extern void player_logout(ENetPeer *peer);
    /* @note after a player is in the world / before they leave it */
    extern void world_enter(ENetPeer *peer, const std::string &world);
    extern void world_leave(ENetPeer *peer, const std::string &world);

    /* @return true if a script has this command (it ran, or the player was told they can't use it) */
    extern bool command(ENetPeer *peer, std::string_view name, std::string_view text);
    /* @return the script commands this player may use, for /help */
    extern std::vector<std::string> command_names(const ::peer &peer);

    /* @return true if a script hid this chat message */
    extern bool chat(ENetPeer *peer, const std::string &text);

    /* @return true if a script stopped this punch / placing / wrench */
    extern bool punch(ENetPeer *peer, int x, int y, short fg, short bg);
    extern bool place(ENetPeer *peer, int x, int y, short item);
    extern bool wrench(ENetPeer *peer, int x, int y, short fg);
    /* @note a block was broken (after the game handled it) */
    extern void tile_broken(ENetPeer *peer, int x, int y, short item);

    /* @return true if this was a script's dialog (its name starts with lua_) */
    extern bool dialog(ENetPeer *peer, const std::string &header);
}
