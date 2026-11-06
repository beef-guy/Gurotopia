#pragma once

/*
* @brief Geiger Counter, Dead Geiger Counter, Geiger Charger and The Great Chameleon: Master of Disguise.
*   - hold a Geiger Counter: it pings red, yellow, then green as you get closer to the world's hidden radiation source.
*     stand still on it to find something. the counter goes dead and you're Irradiated for 30 minutes.
*   - hold a Dead Geiger Counter for 30 minutes to charge it, or tap it into a Geiger Charger (60 minutes).
*   - hold the Great Chameleon and stand still: you turn into the block you're standing on.
*/

constexpr short GEIGER_COUNTER      = 2204;
constexpr short DEAD_GEIGER_COUNTER = 2286;
constexpr short GEIGER_CHARGER_ITEM      = 4654;
constexpr short GREAT_CHAMELEON     = 7206;

/* @return true when this tile change was a Geiger Charger (loading it, or collecting) */
extern bool radiation_tile_change(ENetEvent &event, ::world &world, const ::gamePacket &gamePacket);

/* @brief the player moved: undo the chameleon disguise, restart "standing still" */
extern void radiation_moved(ENetEvent &event);

/* @brief Geiger pings and finds, charging in hand, the Irradiated mod running out, the chameleon. call often */
extern void radiation_tick();

/* @return true if the Irradiated mod is on */
extern bool irradiated(const ::peer &peer);

/* @return "Active effects" lines for timed mods (Irradiated, Charging Geiger Counter), or "" */
extern std::string radiation_effects(const ::peer &peer);

/* @brief give an item, or drop it at the player's feet if the backpack is full */
extern void give_or_drop(ENetEvent &event, ::world &world, short id, short count);

/* @return true (after telling them) while a player can't pick up items: disguised, or just un-disguised */
extern bool chameleon_pickup_blocked(ENetPeer *peer);

/* @brief /geiger (developers): where this world's radiation source is */
extern void geiger_command(ENetEvent &event, const std::string_view text);
