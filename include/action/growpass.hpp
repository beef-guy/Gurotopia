#pragma once

/*
* @brief the Grow Pass: a monthly season of points and rewards.
*   - play to earn Grow Pass points (breaking blocks, harvesting trees, Crazy Jim's daily quest, fishing, the Geiger Counter, fossils, the daily bonus).
*   - every 100 points is a level. each level has a free reward and a Royal reward to claim.
*   - use a Royal Grow Pass Token for the Royal rewards, double points and a bigger daily bonus this season.
*   - a new season starts on the 1st of every month (UTC) and everyone starts again from 0.
*/

constexpr short ROYAL_GROW_PASS_TOKEN = 11304;

/* @brief give Grow Pass points (doubled for Royal), and say when a level is reached */
extern void grow_pass_points(ENetPeer *peer, int points);

/* @brief called for every block broken and every tree harvested: 1 point per 20 blocks, 1 per 10 trees */
extern void grow_pass_break(ENetPeer *peer);
extern void grow_pass_harvest(ENetPeer *peer);

/* @brief the Royal Grow Pass Token was used. @return false if it shouldn't be used up */
extern bool grow_pass_royal_token(ENetEvent &event);

/* @brief the self wrench "bonus" button, and the daily reward buttons */
extern void show_grow_pass(ENetEvent &event, const std::string &tab = "");

/* @note dialog_return "grow_pass" */
extern void grow_pass_edit(ENetEvent &event, const ::hPipe &hPipe);

/* @brief the peer's points/claims belong to this season. called at login */
extern void grow_pass_season_check(::peer &peer);

/* @brief /passpoints <amount> (developers): give yourself Grow Pass points, for testing */
extern void passpoints_command(ENetEvent &event, const std::string_view text);
