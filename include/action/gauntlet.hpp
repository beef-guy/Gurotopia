#pragma once

/*
* @brief Gauntlet of Elements, like real Growtopia (Summer Clash 2018):
*   - Raw Gauntlet of Elements holds no stone and can't move anything.
*   - consume an Elemental Stone on yourself while wearing the right tier (each one also uses up a Soul Stone):
*       Raw + Earth Stone -> Tier I, Tier I + Wind Stone -> Tier II, Tier II + Fire Stone -> Tier III,
*       Tier III + Death Stone -> Tier IV, Tier IV + Water Stone -> Tier V, Tier V + Life Stone -> Exquisite.
*   - in your own world, punch a block with the gauntlet to pick it up, then punch another tile to swap the two.
*     Tier I moves Earth chi blocks, II adds Wind, III adds Fire, V adds Water (V and Exquisite move nearly any block).
*   - the death and respawn looks of Tier IV, V and Exquisite are drawn by the game itself.
*/

constexpr short RAW_GAUNTLET       = 6866;
constexpr short GAUNTLET_TIER_I    = 6868;
constexpr short GAUNTLET_TIER_II   = 6870;
constexpr short GAUNTLET_TIER_III  = 6872;
constexpr short GAUNTLET_TIER_IV   = 6874;
constexpr short GAUNTLET_TIER_V    = 6876;
constexpr short GAUNTLET_EXQUISITE = 6878;
constexpr short SOUL_STONE  = 5202;
constexpr short LIFE_STONE  = 6880;
constexpr short WIND_STONE  = 6882;
constexpr short DEATH_STONE = 6884;
constexpr short FIRE_STONE  = 6886;
constexpr short WATER_STONE = 6888;
constexpr short EARTH_STONE = 6890;

enum chi : u_char { CHI_NONE = 0, CHI_EARTH = 1, CHI_WIND = 2, CHI_FIRE = 4, CHI_WATER = 8 };

/* @return the chi of a block (chi::). @note the real game keeps chi on its server, so this is a best guess from the item's name */
extern u_char block_chi(const ::item &item);

/* @return true if `id` is one of the Elemental Stones */
inline bool is_elemental_stone(short id) { return id >= LIFE_STONE && id <= EARTH_STONE && id % 2 == 0; }

/* @brief an Elemental Stone was used on yourself. upgrades the worn gauntlet */
extern void elemental_stone_use(ENetEvent &event, short stone);

/* @return true when this punch was taken by the Gauntlet of Elements (picking up or swapping a block) */
extern bool gauntlet_punch(ENetEvent &event, ::world &world, const ::gamePacket &gamePacket);
