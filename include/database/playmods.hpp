#pragma once

#include <array>

/* @note powers that clothing gives the wearer, like the real game's playmods ("Double Jump", "Speedy", ...) */
enum mod : u_short {
    M_DOUBLE_JUMP      = 0x0001,
    M_SPEEDY           = 0x0002,
    M_HIGH_JUMP        = 0x0004,
    M_SLOW_FALL        = 0x0008,
    M_PUNCH_RANGE      = 0x0010, // @note +1 tile reach with the Fist
    M_BUILD_RANGE      = 0x0020, // @note +1 tile reach with everything else
    M_PUNCH_POWER      = 0x0040, // @note pushes players further
    M_WATER_SPEED      = 0x0080,
    M_PUNCH_DAMAGE     = 0x0100, // @note message only for now
    M_FIREPROOF        = 0x0200, // @note message only for now
    M_ENHANCED_DIGGING = 0x0400, // @note message only for now
    M_SLOW             = 0x0800  // @note "Putt Putt Putt"
};

struct playmod {
    u_short item{};
    u_short mods{}; // @note mod::
    const char *name{}; // @note the mod's name as the real game shows it
};

/* @return the playmod `item_id` gives when worn, or nullptr */
extern const ::playmod *get_playmod(u_short item_id);

/* @note what the worn clothes add up to, sent in SET_CHARACTER_STATE */
struct cloth_effects {
    u_short mods{};
    u_char punch_range{}; // @note extra tiles
    u_char build_range{}; // @note extra tiles
    float speed{ 250.0f };
    float gravity{ 1000.0f };
    float punch_strength{ 200.0f };
    float water_speed{ 125.0f };
};
extern ::cloth_effects get_cloth_effects(const std::array<float, 10ull> &clothing);

/* @note "`oYou can double jump! (`$Double Jump `omod added)``" in the console, like the real game */
extern void playmod_message(ENetPeer *peer, u_short item_id, bool added);
