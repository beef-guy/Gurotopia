#include "pch.hpp"

#include "onVariant/ConsoleMessage.hpp"
#include "onVariant/SetBux.hpp"
#include "tools/time.hpp"

#include "sb.hpp"

#include <algorithm>

namespace
{
    constexpr u_int cooldown = 60; // @note seconds between two /sb from the same player
}

void sb(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    std::string message{ command_arg(text) }; // @note the whole message, not only the first word
    std::erase_if(message, [](char c) { return c == '|' || c == '\n' || c == '\r'; });
    if (message.size() > 120ull) message.resize(120ull);

    const u_int cost = std::max<u_int>(host->connectedPeers, 1u) * 30;

    if (message.empty())
    {
        send_gameMessage(event.peer, {.message = std::format(
            "action|log\nmsg|`5>> This lets you send a message to {}`` users online and make an annoying beep noise.  Costs `${} Gems`` to use. Example:`` `w/sb Hey guys, come to my world!",
            host->connectedPeers, cost)
        });
        return;
    }
    if (pPeer->role < MODERATOR) // @note staff skip the cost and the wait
    {
        const u_int now = ticks();
        if (pPeer->last_sb != 0 && now - pPeer->last_sb < cooldown)
        {
            on::ConsoleMessage(event.peer, std::format("`4Please wait {} more seconds before using /sb again.``", cooldown - (now - pPeer->last_sb)));
            return;
        }
        if (pPeer->gems < cost)
        {
            on::ConsoleMessage(event.peer, std::format("`4You need {} gems to use /sb, you have {}.``", cost, pPeer->gems));
            return;
        }
        pPeer->gems -= cost;
        pPeer->last_sb = now;
        on::SetBux(event);
    }

    std::string display = pPeer->recent_worlds.back();
    if (auto world = std::ranges::find(worlds, display, &::world::name); world != worlds.end() && world->jammed())
        display = "`4JAMMED!``"; // @note Signal Jammer hides where the broadcast came from
    peers("", PeerSearch::EVERYONE, [&pPeer, message, display](ENetPeer *peer)
    {
        const ::peer *pOthers = static_cast<::peer*>(peer->data);
        if (pOthers == nullptr || !pOthers->authenticated) return;
        on::ConsoleMessage(peer,
            std::format(
                "CP:0_PL:0_OID:_CT:[SB]_ `5** from ({}```5) in [```${}```5] ** : ```${}``",
                pPeer->display_growid, display, message
            )
        );
        send_gameMessage(peer, {.message = "action|play_sfx\nfile|audio/beep.wav\ndelayMS|0"});
    });
}
