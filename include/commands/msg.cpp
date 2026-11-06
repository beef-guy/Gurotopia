#include "pch.hpp"

#include "tools/sound.hpp"

#include "msg.hpp"

void msg(ENetEvent& event, const std::string_view text)
{
    std::vector<std::string> spaces = readch(std::string{ text }, ' ');
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    std::string growid = (spaces.size() > 1) ? spaces[1] : "";
    std::string msg;
    for (std::size_t i = 2/*@note skip growid*/; i < spaces.size(); ++i)
    {
        msg += spaces[i];
        if (i < spaces.size() - 1) msg += " ";
    }
    send_gameMessage(event.peer, {.message = std::format("action|log\nmsg|CT:[MSG]_ `6/msg {} {}``", growid, msg)});

    if (growid.empty() || msg.empty())
    {
        send_gameMessage(event.peer, {.message = "action|log\nmsg|Usage: /msg <`$full or first part of a name``> <`$your message``> - This will send a private message to someone anywhere in the universe.  If you don't include a message, you can just see if he/she is online or not."});
    }
    else {
        bool online = false;
        peers("", PeerSearch::EVERYONE, [&event, &pPeer, &online, growid, msg](ENetPeer *peer)
        {
            ::peer *pOthers = static_cast<::peer*>(peer->data);
            if (online || pOthers == nullptr || pOthers->growid != growid) return;

            online = true;
            send_gameMessage(event.peer, {.message = std::format("action|log\nmsg|CT:[MSG]_`6>> (Sent to `${}``)``", growid)});
            send_gameMessage(peer, {.message = std::format("action|log\nmsg|CT:[MSG]_`6>> Private msg from `${}``: `${}``", pPeer->growid, msg)});
            play_sfx(peer, "audio/pay_time.wav");
        });
        if (!online)
        {
            std::string hint = (growid.length() < 5) ? " (you need to enter at least 5 letters if you meant a longer name!)" : "";
            send_gameMessage(event.peer, {.message = std::format("action|log\nmsg|`6>> `{}` is not online right now{}.``", growid, hint)});
        }
    }
}
