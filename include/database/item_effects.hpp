#pragma once

#include <array>
#include <string>

/*
 * Item effects: buffs an item gives while it's worn or held, set on the website's Item Effects page
 * (web/src/effects.js). They're on top of the item's own powers (double jump, speedy...: playmods.hpp).
 *
 * - gems: % more gems from breaking blocks
 * - xp: % more XP from everything
 * - one_hit: blocks break in one punch
 * - punch_range / build_range: extra tiles of reach for breaking / placing
 * - drops: % chance of one more block or seed when breaking (every 100% is one more for sure)
 * - harvest: % more fruit from trees
 * - bonus_item: an item of the owner's choice that can drop when breaking a block,
 *   bonus_chance in hundredths of a percent (1 = 0.01%, 10000 = always), bonus_amount of it
 *
 * Everything worn adds up. Rows live in this server's `item_effects` table, and a change on
 * the website applies to everyone online right away (panel action effects_reload).
 */
struct item_effect
{
    u_short item{};
    int gems{};
    int xp{};
    bool one_hit{};
    int punch_range{};
    int build_range{};
    int drops{};
    int harvest{};
    u_short bonus_item{};
    int bonus_chance{}; // @note hundredths of a percent
    int bonus_amount{1};
};

/* @note what everything a player wears adds up to */
struct effect_totals
{
    int gems{};
    int xp{};
    bool one_hit{};
    int punch_range{};
    int build_range{};
    int drops{};
    int harvest{};
};

namespace item_effects
{
    constexpr int MAX_PERCENT = 10000;
    constexpr int MAX_RANGE = 10;

    /* @note creates the table, then reads it */
    extern void init();
    /* @note re-reads the table (the website changed it) and sends everyone online their new reach */
    extern void reload();

    /* @return the item's effects, or nullptr if it has none (or they're switched off) */
    extern const ::item_effect *get(u_short item);
    extern ::effect_totals totals(const std::array<float, 10ull> &clothing);
    inline ::effect_totals totals(const ::peer &peer) { return totals(peer.clothing); }

    /* @return `base` with `percent` more, the leftover fraction rounded up by chance (base 3 at +50% is 4 or 5) */
    extern long long boost(long long base, int percent);
    /* @return how many extra drops `percent` gives: one per full 100%, then a chance for one more */
    extern int extra_rolls(int percent);
    /* @note after a block breaks: each worn item with a bonus drop rolls its chance and drops its item at `pos` */
    extern void bonus_drops(ENetEvent &event, const ::peer &peer, const ::pos &pos, ::world &world);

    /* @return "add_label_with_icon|small|..." lines for the wrench menu's Active effects */
    extern std::string active_lines(const std::array<float, 10ull> &clothing);
    /* @return "+450% gems, one-hit breaking" */
    extern std::string describe(const ::item_effect &fx);
    /* @note "Golden Scarf: +450% gems (item effect added)" in the console */
    extern void equip_message(ENetPeer *peer, u_short item, bool added);
}
