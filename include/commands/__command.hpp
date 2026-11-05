#pragma once

#include <unordered_map>

struct cmd_entry
{
    void(*run)(ENetEvent&, std::string_view);
    u_char role{}; // @note lowest role that may use it (PLAYER, MODERATOR, DEVELOPER)
};

extern const std::unordered_map<std::string_view, ::cmd_entry> cmd_pool;

/* @return true if this peer may use the command. /find is open to everyone on a --sandbox server */
extern bool can_use(const ::peer &peer, std::string_view name, const ::cmd_entry &entry);
