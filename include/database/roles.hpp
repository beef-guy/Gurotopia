#pragma once

#include <unordered_set>

/*
 * Roles are kept in this server's `roles` table so the website (web/src/roles.js) can create
 * and edit them. Ids 0 Player, 1 Moderator and 2 Developer always exist.
 * - priority: a role can only moderate (kick, ban, give roles to) roles with a lower priority
 * - powers: the built-in staff powers a role has (PLAYER, MODERATOR or DEVELOPER), which is
 *   what peer::role holds, so every `role >= MODERATOR` check keeps working
 * - commands: the commands the role may use. Developer (id 2) may use every command.
 */
struct role_def
{
    int id{};
    std::string name{"Player"};
    int priority{};
    std::string color{}; // @note a Growtopia colour code (6, #, 2 ...) or empty for the normal name colour
    std::string tag{};   // @note shown before the name, like @
    u_char powers{};
    std::unordered_set<std::string> commands{};
};

namespace roles
{
    /* @note creates the table with the three built-in roles, and lists every command for the website */
    extern void init();
    /* @note re-reads the table, then brings everyone online in line with it (powers and name colour) */
    extern void reload();
    /* @return the role, or Player when that id doesn't exist (any more) */
    extern const role_def &get(int id);
    extern const role_def *find(const std::string &name_or_id);
    extern bool allows(int id, std::string_view command);
    /* @return what the role's powers mean for peer::role */
    extern u_char powers(int id);
    /* @note gives an online player their role's powers and shows the new name to everyone in their world */
    extern void apply(ENetPeer *peer);
    /* @note works out the name colour from the role, or else from the world (owner green, access yellow) */
    extern std::string display_name(const ::peer &peer);
    /* @note tells everyone in the player's world about their (new) name */
    extern void refresh_name(ENetPeer *peer);
}
