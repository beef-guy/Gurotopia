#include "pch.hpp"
#include "tools/sound.hpp"

#include "respawn_spike.hpp"

void action::respawn_spike(ENetEvent& event, const std::string& header) 
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    send_varlist(event.peer, { "OnSetPos", CL_Vec2f{pPeer->rest_pos.x, pPeer->rest_pos.y} }, pPeer->netid, 0x000007d0/*2000ms*/);
    play_sfx(event.peer, "audio/teleport.wav", 2000);
    send_varlist(event.peer, { "OnSetFreezeState", 0u }, pPeer->netid, 0x000007d0/*2000ms*/);
}