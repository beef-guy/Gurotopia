#include "pch.hpp"
#include "tools/sound.hpp"
#include "action/special_items.hpp"
#include "action/carnival.hpp"

#include "respawn.hpp"

void action::respawn(ENetEvent& event, const std::string& header) 
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    special_respawn(event); // @note a carried Game Flag goes back, a challenge fails
    carnival_respawn(event); // @note out of a Carnival game

    send_varlist(event.peer, { "OnSetFreezeState", 2u }, pPeer->netid);
    send_varlist(event.peer, { "OnKilled"}, pPeer->netid);

    send_varlist(event.peer, { "OnSetPos", CL_Vec2f{pPeer->rest_pos.x, pPeer->rest_pos.y} }, pPeer->netid, 0x000007d0/*2000ms*/);
    play_sfx(event.peer, "audio/teleport.wav", 2000);
    send_varlist(event.peer, { "OnSetFreezeState", 0u }, pPeer->netid, 0x000007e4/*2020ms*/);
}