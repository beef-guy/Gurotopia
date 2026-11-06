#include "pch.hpp"

#include "action/respawn.hpp"
#include "action/fishing.hpp"
#include "action/radiation.hpp"
#include "action/special_items.hpp"

#include "movement.hpp"

void movement(ENetEvent& event, ::gamePacket gamePacket) 
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (pPeer->netid == 0) return; // @note not in a world

    pPeer->pos = gamePacket.pos;
    pPeer->facing_left = gamePacket.state & (int)gamePacketState::MOVE_LEFT;
    fishing_moved(event);
    radiation_moved(event); // @note Geiger Counter and Great Chameleon want you standing still
    special_moved(event); // @note Challenge Flags, Game Flags and Goals

    /* add fireproof only take away 1 hp instead of 2 */
    if (gamePacket.state & (int)gamePacketState::HIT_BY_LAVA) pPeer->pain_hp -= 2;
    if (pPeer->pain_hp <= 0) action::respawn(event, ""), pPeer->pain_hp = 10;
    
    gamePacket.netid = pPeer->netid;
    send_gamePacket(*event.peer, gamePacket); // finished.
}
