#pragma once

/*
 * The website (web/) talks to a running server through two tables in its database:
 * - panel_actions: the website adds a row (kick a player, re-read a player after a ban,
 *   mute or role change, empty or delete a world) and this server carries it out within a second.
 * - panel_live: who is online right now and which world they are in, rewritten every few seconds.
 */
extern void panel_bridge_create_tables();
extern void panel_tick();
extern void panel_bridge_clear(); // @note on shutdown: nobody is online any more
extern void panel_items_catalog(); // @note every item, for the website's item manager (web/src/items.js)
