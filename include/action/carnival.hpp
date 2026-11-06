#pragma once

/*
* @brief the Carnival, like real Growtopia (action/carnival.cpp):
*   - it comes to town for 3 days every month (Friday to Sunday after the first Friday, UTC). /carnival on|off|auto (developers) overrides it.
*   - the world CARNIVAL (and CARNIVAL1 to CARNIVAL50) is built by the server: tents, Golden Ticket Booths, the Ringmaster and every game room.
*   - Golden Ticket Booth: 100 rarity of any items = 1 Golden Ticket (extra rarity is kept as credit), 1 World Lock = 30 tickets.
*     a worn Ring of Savings makes it 90 rarity and 33 tickets.
*   - each game costs 1 Golden Ticket at its Carnival Tent. win it for a Common, Rare or Ultimate prize (1, 2 or 3 fireworks),
*     sometimes a Mystery Prize, and a Funtime Block after 50 wins.
*   - games: Shooting Gallery, Growganoth Gulch, Mirror Maze, Concentration, Brutal Bounce, Death Race 5000, Spiky Survivor, Waste a Ticket.
*   - The Ringmaster: level 20+, 10 Golden Tickets for 10 random tasks, then a random Ring. 5 of the same Ring trade for another.
*/

constexpr short GOLDEN_TICKET = 1898;
constexpr short THE_RINGMASTER = 1900;
constexpr short GOLDEN_TICKET_BOOTH = 1902;

/* @return true if this world is CARNIVAL or CARNIVAL1..CARNIVAL50 */
extern bool is_carnival_world(const std::string &name);
/* @return true while the Carnival is in town */
extern bool carnival_open();

/* @brief a carnival world is about to be sent to its first visitor: (re)build it */
extern void carnival_prepare(::world &world);

/* @return true when this tile change was handled here (game punches, the Ringmaster, the booth, or a blocked edit) */
extern bool carnival_tile_change(ENetEvent &event, ::world &world, ::gamePacket &gamePacket);
/* @return true when a Carnival Tent was walked into */
extern bool carnival_activate(ENetEvent &event, ::world &world, const ::pos &tile);
extern void carnival_moved(ENetEvent &event);
/* @brief the player died: called before they are moved to their respawn position */
extern void carnival_respawn(ENetEvent &event);
/* @brief the player is leaving `world` (before they are removed from it) */
extern void carnival_left_world(ENetEvent &event, ::world &world);
/* @brief game timers, moving targets, platforms. call often */
extern void carnival_tick();

/* @brief Ringmaster quest tasks that count up while playing anywhere */
enum ring_task : int
{
    RT_NONE, RT_DELIVER_ITEM, RT_HARVEST_RARITY, RT_PLANT_RARITY, RT_BREAK, RT_BREAK_RARITY, RT_XP, RT_GEMS, RT_WORLD_LOCKS,
    RT_CARNIVAL_WINS, RT_PROVIDERS, RT_GEIGER, RT_FISH_LBS, RT_FOSSILS,
    RT_COUNT
};
/* @brief something happened that a Ringmaster task may count (amount: blocks, rarity, XP, lbs...) */
extern void ring_progress(ENetPeer *peer, int task, long long amount);

/* @brief peer::carnival as text for the database, and back */
extern std::string carnival_serialize(const ::peer &peer);
extern void carnival_load(::peer &peer, const std::string &text);

/* @brief Goals & Quests (wrench menu): one line about the Ringmaster's quest, and the quest page */
extern std::string carnival_goal_line(const ::peer &peer);
extern void carnival_goal_dialog(ENetPeer *peer);

/* @brief dialog_return handlers */
extern void carnival_booth_edit(ENetEvent &event, const ::hPipe &hPipe);
extern void carnival_ring_edit(ENetEvent &event, const ::hPipe &hPipe);

/* @brief /carnival: when the Carnival is in town. developers: /carnival on|off|auto */
extern void carnival_command(ENetEvent &event, const std::string_view text);
