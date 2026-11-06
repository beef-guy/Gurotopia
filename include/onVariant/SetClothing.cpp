#include "pch.hpp"

#include "database/playmods.hpp"
#include "action/radiation.hpp"
#include "action/special_items.hpp"
#include "action/ghosts.hpp"

#include "SetClothing.hpp"

void on::SetClothing(ENetPeer *peer, bool sound, bool everyone)
{
    ::peer *pPeer = static_cast<::peer*>(peer->data);

    /* @note Go-Go-Growformer! robot form: the Growformer PowerKey worn on the back is the robot, the player's own body is hidden (state 0x3c) */
    const bool robot = pPeer->transformed == 7384;
    const auto c = [pPeer, robot](int slot) {
        if (pPeer->disguised) return 0.0f; // @note the Great Chameleon: no clothes while disguised
        if (robot && slot == clothing::BACK) return 7480.0f;
        return pPeer->clothing[slot];
    };
    const VariantList clothes = {
        "OnSetClothing", 
        CL_Vec3f{c(clothing::HAIR), c(clothing::SHIRT), c(clothing::LEGS)}, 
        CL_Vec3f{c(clothing::FEET), c(clothing::FACE), c(clothing::HAND)}, 
        CL_Vec3f{c(clothing::BACK), c(clothing::HEAD), c(clothing::CHARM)}, 
        (pPeer->state & S_GHOST) ? -140 : pPeer->skin_color,
        CL_Vec3f{c(clothing::ANCES), sound ? 1.0f : 0.0f, 0.0f} // @note y != 0: the client plays audio/change_clothes.wav
    };
    send_varlist(peer, clothes, pPeer->netid);
    if (everyone && pPeer->netid != 0)
        peers(pPeer->recent_worlds.back(), PeerSearch::WORLD, [&](ENetPeer *p) { if (p != peer) send_varlist(p, clothes, pPeer->netid); });

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
    pPeer->mods = fx.mods;

    ::gamePacket gamePacket {
        /* @note 0x14 PACKET_SET_CHARACTER_STATE. byte 1: punch effect, byte 2: 0x80 + extra build range, byte 3: 0x80 + extra punch (Fist) range */
        .type = static_cast<int>(0x14u | (u_int(pPeer->punch_effect) << 8) | (u_int(0x80 + fx.build_range) << 16) | (u_int(0x80 + fx.punch_range) << 24)),
        .netid = pPeer->netid,
        .waterspeed = fx.water_speed,
        .id = pPeer->state | ((fx.mods & M_DOUBLE_JUMP) ? S_DOUBLE_JUMP : 0) | (irradiated(*pPeer) ? S_IRRADIATED : 0) | (cursed(*pPeer) ? S_DEVIL_HORNS : 0) | (robot ? 0x3c : 0), // @note 04 invisible (only eyes/mouth), 08 no arms, 16 no face, 32 invisible (only legs/arms), 64 devil horns, 128 angel halo, 2048 frozen, 4096 gray skin?,8192 ducttape, 16384 Onion effect, 32768 stars effect, 65536 zombie, 131072 hit by lava, 262144 shadow effect, 524288 irradiated effect, 1048576 spotlight, 2097152 pineapple thingy
        .pos = ::pos{ 1200.0f/*acceleration*/, fx.punch_strength },
        .speed = ::pos{ fx.speed * ghost_speed_factor(*pPeer), fx.gravity }, // @note slimed by a ghost: slower, or turned around by a Mind Ghost
        .punch = ::pos{ pPeer->hair_color, 0x00000000u } // @todo can this even be unsigned?
    };
    send_gamePacket(*peer, gamePacket); // @todo handle for 'p'
}