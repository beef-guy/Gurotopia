#pragma once

/*
* @brief fossils like real Growtopia:
*   - Rock (and Boulder, Igneous Rock, Asteroid, Mars Rock, Deep Rock) in a world people are in now and then turns into a Fossil Rock.
*     the more people are in the world, the likelier.
*   - tap it carefully with a Rock Hammer until "I unearthed a Fossil!". one more hammer tap smashes it.
*   - break it with a Rock Chisel to get the Fossil out (anything else destroys the fossil).
*   - use a Fossil Brush on the Fossil before anyone picks it up to get a Polished Fossil.
*   - put a Fossil or Polished Fossil in a Fossil Prep Station, wait, and punch it to see what it was.
*/

constexpr short ROCK_HAMMER   = 3932;
constexpr short ROCK_CHISEL   = 3934;
constexpr short FOSSIL_ITEM        = 3936;
constexpr short FOSSIL_BRUSH  = 4132;
constexpr short POLISHED_FOSSIL = 4134;

/* @return true when this tile change was handled here (hammer tap, brush, loading or collecting a Prep Station) */
extern bool fossil_tile_change(ENetEvent &event, ::world &world, const ::gamePacket &gamePacket);

/* @brief a Fossil Rock was broken. @return true if it should drop nothing else */
extern bool fossil_broken(ENetEvent &event, ::world &world, const ::pos &pos, bool open);

/* @brief turn a random rock in this world into a Fossil Rock. @return false if there's no rock */
extern bool fossil_spawn(::world &world, ::pos *where = nullptr);

/* @brief now and then, rocks in busy worlds turn into Fossil Rocks. call often */
extern void fossil_tick();

/* @brief /fossil (developers): a Fossil Rock appears in this world now */
extern void fossil_command(ENetEvent &event, const std::string_view text);

/* @brief /ready (developers): every Fossil Prep Station and Geiger Charger in this world finishes now */
extern void ready_command(ENetEvent &event, const std::string_view text);
