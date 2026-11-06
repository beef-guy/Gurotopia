#pragma once

/*
* @brief special items with their own rules (action/special_items.cpp):
*   - Freeze Wand (274), Curse Wand (278), Ban Wand (732): used on another player.
*   - Kranken's Galactic Block and its patterns: the tint is picked in the wrench menu.
*   - Safe Vault (8878): items behind a password.
*   - Game Generator (1660) with Staging Platforms, Game Blocks, Game Flags, Game Graves and Game Goals: team games.
*   - Challenge Timer (3804), Challenge Start Flag (3806), Challenge End Flag (3808): timed runs.
*/

constexpr short FREEZE_WAND = 274;
constexpr short CURSE_WAND  = 278;
constexpr short BAN_WAND    = 732;

/* @return true when this tile change was handled here (a wand on a player, a Game Block punched during a game, ...) */
extern bool special_tile_change(ENetEvent &event, ::world &world, ::gamePacket &gamePacket, ::block &block);

/* @return true if the wrench menu was opened here */
extern bool special_wrench(ENetEvent &event, ::world &world, const ::pos &pos, const ::item &item);

/* @brief a block was placed: forget what the last one on this tile had */
extern void special_placed(::world &world, const ::pos &pos, const ::item &item);
/* @brief a block was broken: forget its data */
extern void special_broken(::world &world, const ::pos &pos);
/* @return an error to show if this block can't be broken now ("" = it can) */
extern std::string special_break_blocked(::world &world, const ::pos &pos, const ::item &item);

/* @brief the player moved: challenge flags, game flags and goals */
extern void special_moved(ENetEvent &event);
/* @brief the player respawned: drop a carried flag, go back to the team's grave */
extern void special_respawn(ENetEvent &event);
/* @brief the player is leaving their world (call before they are removed from it) */
extern void special_left_world(ENetEvent &event, ::world &world);
/* @brief the player just entered a world: show a running game */
extern void special_entered(ENetEvent &event, ::world &world);

/* @return the world a player has to go to instead of `name`, or "" (Curse Wand: HELL); throws if they are banned from it */
extern std::string special_join_check(ENetEvent &event, const std::string &name);
/* @return true while the Curse Wand's curse lasts */
extern bool cursed(const ::peer &peer);

/* @brief thaw frozen players, lift curses, run out challenge timers and games. call often */
extern void special_tick();

/* @brief dialog_return handlers */
extern void kranken_edit(ENetEvent &event, const ::hPipe &hPipe);
extern void vault_edit(ENetEvent &event, const ::hPipe &hPipe);
extern void game_rules_edit(ENetEvent &event, const ::hPipe &hPipe);
extern void game_tile_edit(ENetEvent &event, const ::hPipe &hPipe);
extern void challenge_edit(ENetEvent &event, const ::hPipe &hPipe);
