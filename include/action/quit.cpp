#include "pch.hpp"

#include "action/quit_to_exit.hpp"

#include "quit.hpp"

void action::quit(ENetEvent& event, const std::string& header) 
{
    if (event.peer == nullptr || event.peer->data == nullptr) return; // @note already quit

    action::quit_to_exit(event, "", true);

    delete static_cast<::peer*>(event.peer->data);
    event.peer->data = nullptr;
    enet_peer_reset(event.peer);
}