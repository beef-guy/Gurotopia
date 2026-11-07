#pragma once

/*
 * Machine capacities: how much a machine holds, set on the website's Machine Capacities page (web/src/machines.js).
 * - magplants (MAGPLANT 5000, Gaia's Beacon, Unstable Tesseract and custom ones copied from them):
 *   capacity, what they take (anything, seeds only, no seeds) and whether they have building mode (the remote)
 * - vending machines: the most items one holds (0 = no limit)
 * - the Techno-Organic Engine: the most drops it keeps
 *
 * Rows live in this server's `machines` table. The game adds a row for every machine in items.dat on start
 * (with the real game's numbers), and a change on the website applies right away (panel action machines_reload).
 */
namespace machines
{
    enum accepts : u_char { ANY = 0, SEEDS_ONLY = 1, NO_SEEDS = 2 };
    constexpr int MAX_CAPACITY = 1'000'000'000;

    /* @note creates the table, adds the game's machines, then reads it. needs items.dat decoded */
    extern void init();
    /* @note re-reads the table (the website changed it) */
    extern void reload();

    /* @return true for an item this page covers: a magplant, a vending machine or the Techno-Organic Engine */
    extern bool is_machine(short item);
    /* @return the most items the machine holds. 0 = no limit (only vending machines can have that) */
    extern int capacity(short item);
    /* @return what a magplant takes */
    extern accepts takes(short item);
    /* @return true if punching this magplant turns building mode on (and hands out the MAGPLANT 5000 Remote) */
    extern bool has_remote(short item);
}
