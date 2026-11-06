#include "pch.hpp"

#include "database/playmods.hpp"

#include "SetClothing.hpp"

void on::SetClothing(ENetPeer *peer, bool sound)
{
    ::peer *pPeer = static_cast<::peer*>(peer->data);

    send_varlist(peer, {
        "OnSetClothing", 
        CL_Vec3f{pPeer->clothing[clothing::HAIR], pPeer->clothing[clothing::SHIRT], pPeer->clothing[clothing::LEGS]}, 
        CL_Vec3f{pPeer->clothing[clothing::FEET], pPeer->clothing[clothing::FACE], pPeer->clothing[clothing::HAND]}, 
        CL_Vec3f{pPeer->clothing[clothing::BACK], pPeer->clothing[clothing::HEAD], pPeer->clothing[clothing::CHARM]}, 
        (pPeer->state & S_GHOST) ? -140 : pPeer->skin_color,
        CL_Vec3f{pPeer->clothing[clothing::ANCES], sound ? 1.0f : 0.0f, 0.0f} // @note y != 0: the client plays audio/change_clothes.wav
    }, pPeer->netid);

    /* @note powers from worn clothing (double jump, speedy, ...). @todo playmods from consumables */
    u_int xenonite{};
    if (pPeer->netid != 0)
    {
        auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
        if (world != worlds.end()) xenonite = world->xenonite; // @note a Xenonite Crystal forces or blocks powers for everyone here
    }
    const ::cloth_effects fx = get_cloth_effects(pPeer->clothing, xenonite);
    pPeer->punch_range = fx.punch_range;
    pPeer->build_range = fx.build_range;

    ::gamePacket gamePacket {
        /* @note 0x14 PACKET_SET_CHARACTER_STATE. byte 1: punch effect, byte 2: 0x80 + extra build range, byte 3: 0x80 + extra punch (Fist) range */
        .type = static_cast<int>(0x14u | (u_int(pPeer->punch_effect) << 8) | (u_int(0x80 + fx.build_range) << 16) | (u_int(0x80 + fx.punch_range) << 24)),
        .netid = pPeer->netid,
        .waterspeed = fx.water_speed,
        .id = pPeer->state | ((fx.mods & M_DOUBLE_JUMP) ? S_DOUBLE_JUMP : 0), // @note 04 invisible (only eyes/mouth), 08 no arms, 16 no face, 32 invisible (only legs/arms), 64 devil horns, 128 angel halo, 2048 frozen, 4096 gray skin?,8192 ducttape, 16384 Onion effect, 32768 stars effect, 65536 zombie, 131072 hit by lava, 262144 shadow effect, 524288 irradiated effect, 1048576 spotlight, 2097152 pineapple thingy
        .pos = ::pos{ 1200.0f/*acceleration*/, fx.punch_strength },
        .speed = ::pos{ fx.speed, fx.gravity },
        .punch = ::pos{ pPeer->hair_color, 0x00000000u } // @todo can this even be unsigned?
    };
    send_gamePacket(*peer, gamePacket); // @todo handle for 'p'
}