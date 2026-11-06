#include "pch.hpp"

#include "onVariant/Action.hpp"

#include "msg.hpp"
#include "sb.hpp"
#include "time.hpp"
#include "who.hpp"
#include "me.hpp"
#include "find.hpp"
#include "warp.hpp"
#include "punch.hpp"
#include "skin.hpp"
#include "news.hpp"
#include "weather.hpp"
#include "ghost.hpp"
#include "moderation.hpp"
#include "https/server_data.hpp"

#include <algorithm>

#include "__command.hpp"

bool can_use(const ::peer &peer, std::string_view name, const ::cmd_entry &entry)
{
    if (name == "/find" && gServer_data.sandbox) return true;
    if (name == "/playermode") return peer.player_mode || peer.role >= DEVELOPER; // @note so it can be switched off again
    return peer.role >= entry.role;
}

/* if you plan to use this outside of this file, please include in __command.hpp (^-^) - and just make it a void. */
void help_return(ENetEvent& event, const std::string_view text)
{
    const ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    std::vector<std::string_view> names{};
    for (const auto &[name, entry] : cmd_pool)
        if (entry.run != on::Action && name != "/?" && can_use(*pPeer, name, entry)) names.emplace_back(name);
    std::ranges::sort(names);

    std::string list{};
    for (std::string_view name : names) list += std::format("{} ", name);
    send_gameMessage(event.peer, {.message = std::format("action|log\nmsg|>> Commands: {}", list)});
    send_gameMessage(event.peer, {.message = "action|log\nmsg|>> Emotes: /wave /dance /love /sleep /facepalm /fp /smh /yes /no /omg /idk /shrug /furious /rolleyes /foldarms /stubborn /fold /dab /sassy /dance2 /march /grumpy /shy"});
}

const std::unordered_map<std::string_view, ::cmd_entry> cmd_pool
{
    {"/help", {help_return}},
    {"/?", {help_return}},
    {"/msg", {msg}},
    {"/sb", {sb}},
    {"/time", {command::time}}, // @note namespace is to prevent mismatching C time
    {"/who", {who}},
    {"/me", {me}},
    {"/warp", {warp}},
    {"/skin", {skin}},
    {"/news", {news}},
    {"/weather", {weather}}, // @note only changes the weather on the player's own screen

    /* @note staff */
    {"/ghost", {ghost, MODERATOR}},
    {"/kick", {kick, MODERATOR}},
    {"/ban", {ban, MODERATOR}},
    {"/unban", {unban, MODERATOR}},
    {"/mute", {mute, MODERATOR}},
    {"/unmute", {unmute, MODERATOR}},
    {"/pull", {pull, MODERATOR}},
    {"/warpto", {warpto, MODERATOR}},
    {"/broadcast", {broadcast, MODERATOR}},
    {"/bc", {broadcast, MODERATOR}},

    /* @note developers: these change the economy */
    {"/find", {find, DEVELOPER}},
    {"/give", {give, DEVELOPER}},
    {"/punch", {punch, DEVELOPER}},
    {"/setrole", {setrole, DEVELOPER}},
    {"/givegems", {givegems, DEVELOPER}},
    {"/trashall", {trashall, DEVELOPER}},
    {"/mail", {mail, DEVELOPER}},
    {"/maillayout", {maillayout, DEVELOPER}},
    {"/playermode", {playermode, PLAYER}}, // @note can_use() lets only developers, or someone already in player mode, use it

    {"/wave", {on::Action}}, {"/dance", {on::Action}}, {"/love", {on::Action}}, {"/sleep", {on::Action}}, {"/facepalm", {on::Action}}, {"/fp", {on::Action}},
    {"/smh", {on::Action}}, {"/yes", {on::Action}}, {"/no", {on::Action}}, {"/omg", {on::Action}}, {"/idk", {on::Action}}, {"/shrug", {on::Action}},
    {"/furious", {on::Action}}, {"/rolleyes", {on::Action}}, {"/foldarms", {on::Action}}, {"/fa", {on::Action}}, {"/stubborn", {on::Action}}, {"/fold", {on::Action}},
    {"/dab", {on::Action}}, {"/sassy", {on::Action}}, {"/dance2", {on::Action}}, {"/march", {on::Action}}, {"/grumpy", {on::Action}}, {"/shy", {on::Action}}
};
