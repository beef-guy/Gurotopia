#pragma once

/*
* @brief ghost hunting like real Growtopia (action/ghosts.cpp):
*   - Ghosts and Mind Ghosts float around worlds. They move into worlds nobody visited for a while, and now and then into busy ones,
*     unless a Ghost Charm is switched on. An Ecto-Lock draws more of them. Up to 25 in a world.
*   - touching a Ghost slows you for 6 seconds (Rubber Boots or Ghostking's Glory stop it). A Mind Ghost turns your controls around
*     for 6 seconds (Foil Hat, Alien Mind Protector or Ghostking's Glory stop it).
*   - Neutron Gun + Neutron Pack: punching fires a beam that grabs ghosts and pulls them in. Crossing beams with someone kills you both.
*   - Ghost Jar / Ghost Trap: set it down, it switches on after 2 seconds and sucks in a ghost above it for 5 seconds.
*     The ghost comes out as a Ghost-In-A-Jar (or Mind-Ghost-In-A-Jar).
*   - Ghost-In-A-Jar: throw it to let the ghost out, or drop it in front of a Spirit Storage Unit to store it for good.
*   - Spirit Storage Unit: each ghost fills it 5%. Over 100% every new ghost may blow it up, and the fuller it was, the better the prize.
*   - Neutron Power Glove + Neutron Pack: punch ghosts to banish them. Ghost-Be-Gone, or calling 52368 for 15 World Locks, clears them all.
* the ghosts are NPCs the game draws and moves itself (game packet 0x22). Packet layout and NPC numbers were read from the game client.
*/

constexpr short NEUTRON_PACK         = 3714;
constexpr short NEUTRON_GUN          = 3716;
constexpr short RUBBER_BOOTS         = 3718;
constexpr short GHOST_JAR            = 3720;
constexpr short GHOST_IN_A_JAR       = 3722;
constexpr short SPIRIT_STORAGE_UNIT  = 3724;
constexpr short DESTROYED_SSU        = 3726;
constexpr short GHOST_CHARM          = 3750;
constexpr short GHOST_BE_GONE        = 6066;
constexpr short MIND_GHOST_IN_A_JAR  = 6080;
constexpr short GHOST_TRAP           = 6082;
constexpr short NEUTRON_POWER_GLOVE  = 6086;

/* @return true when this tile change was handled here (a jar set down, a beam fired, a jar thrown, the SSU wrenched...) */
extern bool ghost_tile_change(ENetEvent &event, ::world &world, const ::gamePacket &gamePacket);

/* @brief the player moved: ghosts they bump into slime them */
extern void ghost_moved(ENetEvent &event);

/* @brief the player just entered a world: ghosts that moved in while it was empty, and every ghost and jar for their screen */
extern void ghost_entered(ENetEvent &event, ::world &world);

/* @brief game packet 0x22 from the game: punching a ghost with the Neutron Power Glove */
extern void ghost_npc_packet(ENetEvent &event, ::gamePacket gamePacket);

/* @brief Ghost-In-A-Jars dropped on a Spirit Storage Unit's tile go in. @return how many it took */
extern int ssu_take_jars(ENetEvent &event, ::world &world, const ::pos &tile, short item, int count);

/* @brief a block was broken or placed here: a Spirit Storage Unit there starts empty */
extern void ghost_tile_cleared(::world &world, const ::pos &pos);

/* @brief every ghost in this world disappears. @return how many there were */
extern int ghosts_clear(::world &world);

/* @return how much slower (or turned around) a slimed player moves: 1 = normal */
extern float ghost_speed_factor(const ::peer &peer);

/* @brief ghosts float about, jars catch them, slime wears off, new ghosts move in. call often */
extern void ghost_tick();

/* @brief /spawnghost [mind] [count] (developers): ghosts appear in this world now */
extern void spawnghost_command(ENetEvent &event, const std::string_view text);
