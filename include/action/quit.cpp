#include "pch.hpp"

#include "action/quit_to_exit.hpp"
#include "database/friends.hpp"
#include "action/dialog_return/wrenchables.hpp"

#include "quit.hpp"

void action::quit(ENetEvent& event, const std::string& header) 
{
    if (event.peer == nullptr || event.peer->data == nullptr) return; // @note already quit

    action::quit_to_exit(event, "", true);
    friends_alert(event.peer, false);
    const int uid = static_cast<::peer*>(event.peer->data)->user_id;

    delete static_cast<::peer*>(event.peer->data);
    event.peer->data = nullptr;
    enet_peer_reset(event.peer);
    heart_monitors_refresh(uid); // @note their Heart Monitors stop beating (after the reset, so this peer is no longer in the list)
}