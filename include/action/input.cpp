#include "pch.hpp"

#include "commands/__command.hpp"
#include "commands/moderation.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "onVariant/SetClothing.hpp"
#include "tools/time.hpp"
#include "tools/random.hpp"

#include "action/profile.hpp"
#include "scripting/lua.hpp"

#include "input.hpp"

#include <algorithm>
#include <ctime>
#include <unordered_map>

extern std::unordered_map<std::string_view, std::string_view> emoticon; // @note onVariant/EmoticonDataChanged.cpp

namespace
{
    /* @return true (after telling them) while this peer is muted */
    bool muted(ENetEvent &event, ::peer &peer)
    {
        const long long now = std::time(nullptr);
        if (peer.muted_until <= now) return false;
        on::ConsoleMessage(event.peer, std::format("`4You are muted for {} more.``", moderation::duration(peer.muted_until - now)));
        return true;
    }
}

void action::input(ENetEvent& event, const std::string& header)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    ::hPipe hPipe{ header };
    std::string text = hPipe["text"];

    const auto is_space = [](char c) { return std::isspace(static_cast<u_char>(c)) != 0; }; // @note non-english letters are negative chars, which ::isspace can't take
    if (text.empty() || text.front() == '\r' || std::ranges::all_of(text, is_space)) return;

    text.erase(text.begin(), std::find_if_not(text.begin(), text.end(), is_space));
    text.erase(std::find_if_not(text.rbegin(), text.rend(), is_space).base(), text.end());
    if (text.empty()) return; // @note we recheck if empty since we did trimming.
    
    const u_int now = ticks();
    pPeer->messages.push_back(now);
    if (pPeer->messages.size() > 5) pPeer->messages.pop_front();
    if (pPeer->messages.size() == 5 && now - pPeer->messages.front() <= 5)
    {
        on::ConsoleMessage(event.peer,
            "`6>>`4Spam detected! ``Please wait a bit before typing anything else.  "  
            "Please note, any form of bot/macro/auto-paste will get all your accounts banned, so don't do it!");
    }
    else if (text.starts_with('/')) 
    {
        send_gameMessage(event.peer, {.message = std::format("action|log\nmsg|`6{}``", text)});

        std::string_view command = text;
        command = command.substr(0, command.find(' '));

        const bool talking = (command == "/sb" || command == "/msg" || command == "/me");
        if (auto it = cmd_pool.find(command); it == cmd_pool.end() || !can_use(*pPeer, command, it->second))
        {
            if (it == cmd_pool.end() && lua::command(event.peer, command, text)) return; // @note a command from the server's Lua scripts
            send_gameMessage(event.peer, {.message = "action|log\nmsg|`4Unknown command.`` Enter `$/?`` for a list of valid commands."});
        }
        else if (talking && muted(event, *pPeer)) return;
        else
            it->second.run(event, std::move(text));
    }
    else 
    {
        if (muted(event, *pPeer)) return;
        if (lua::chat(event.peer, text)) return; // @note a script hid this message (its chat event returned true)
        if ((pPeer->state & S_DUCT_TAPE) && pPeer->duct_tape_until != 0 && std::time(nullptr) >= pPeer->duct_tape_until)
        {
            pPeer->state &= ~S_DUCT_TAPE; // @note duct tape falls off after a while
            pPeer->duct_tape_until = 0;
            on::ConsoleMessage(event.peer, "`2The duct tape fell off, you can talk again!``");
            on::SetClothing(event.peer);
        }
        for (const auto &[code, glyph] : emoticon) // @note a Growmoji they haven't unlocked shows as its name
        {
            if (growmoji_unlocked(*pPeer, code)) continue;
            for (std::size_t at = text.find(glyph); at != std::string::npos; at = text.find(glyph, at))
                text.replace(at, glyph.size(), std::format("({})", code));
        }
        if (pPeer->state & S_DUCT_TAPE)
        {
            for (char &c : text)
                if (c != ' ') c = (RandomRange(1, 3) == 1) ? 'm' : 'f';
        }
        const std::string &player_chat = std::format("CP:0_PL:0_OID:_player_chat={}", text);
        const std::string &message = std::format("CP:0_PL:0_OID:_CT:[W]_ `6<{}>`` `$`${}````", pPeer->display_growid, text);
        peers(pPeer->recent_worlds.back(), PeerSearch::WORLD, [&event, &pPeer, player_chat, message](ENetPeer *p) 
        {
            const ::peer *pListener = static_cast<::peer*>(p->data);
            if (std::ranges::find(pListener->ignored, pPeer->user_id) != pListener->ignored.end()) return; // @note Ignore Player
            send_varlist(p, { "OnTalkBubble", pPeer->netid, player_chat, 0u });
            on::ConsoleMessage(p, message, pPeer->netid);
        });
    }
}
