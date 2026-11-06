#include "pch.hpp"

#include "onVariant/ConsoleMessage.hpp"
#include "onVariant/SetBux.hpp"
#include "onVariant/SetClothing.hpp"
#include "onVariant/Action.hpp"
#include "commands/punch.hpp" // @note get_punch_id()
#include "database/playmods.hpp"
#include "tools/create_dialog.hpp"
#include "tools/random.hpp"
#include "tools/sound.hpp"
#include "action/radiation.hpp" // @note give_or_drop()

#include "carnival.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <unordered_map>

/*
* @note sources: growtopia.fandom.com/wiki/Carnival, /wiki/The_Ringmaster, /wiki/Golden_Ticket, /wiki/Golden_Ticket_Booth.
* @note the wiki doesn't give everything. these are my own picks, marked "@note my pick" where they are used:
*   prize odds, time limits, scores to win, task amounts, how the CARNIVAL world is laid out, the Mystery Prize chance.
*/

namespace
{
    /* ---------------- items ---------------- */
    constexpr short MAIN_DOOR      = 6;
    constexpr short BEDROCK        = 8;
    constexpr short LAVA           = 4;
    constexpr short DEATH_SPIKES   = 162;
    constexpr short WORLD_LOCK     = 242;
    constexpr short DIAMOND_LOCK   = 1796;
    constexpr short CREEPSTONE     = 1222;
    constexpr short EVIL_EYE       = 1226;
    constexpr short FUNHOUSE_MIRROR = 1872;
    constexpr short CARNIVAL_WALLPAPER = 1878;
    constexpr short CARNIVAL_AWNING = 1880;
    constexpr short CARNIVAL_PIPE  = 2238;
    constexpr short CARNIVAL_BLOCK = 1882;
    constexpr short CARNIVAL_SIGN  = 1906;
    constexpr short BULLSEYE       = 1908;
    constexpr short CARNIVAL_TENT  = 1910;
    constexpr short CARNIVAL_LANDING = 1912;
    constexpr short CARNIVAL_PLATFORM = 1914;
    constexpr short CARD_BACK      = 1916;
    constexpr short CARD_FACES[]   = { 1918/*Pentagon*/, 1920/*Star*/, 1922/*Sizzle*/, 1924/*Plus*/ };
    constexpr short MIRROR_MAZE_BLOCK = 1926;
    constexpr short CALLIOPE       = 1942;
    constexpr short CARNIVAL_SPIKEBALL = 2568;
    constexpr short WINTER_WIND    = 3232;
    constexpr short FUNTIME_BLOCK  = 3532;
    constexpr short CREEP_PLATFORM = 7180;
    constexpr short CREEP_PLATFORM_OFF = 7182;
    constexpr short RING_OF_SAVINGS = 3140;
    constexpr short PENNANTS[]     = { 1884, 1886, 1888, 1890 };

    /* @note the Mystery Prizes, oldest first (wiki) */
    constexpr short MYSTERY_PRIZES[] = { 2980/*Jester's Cap*/, 3010/*Licorice Rod*/, 3158/*Carnival Pinwheel*/, 3802/*Globe*/, 8842/*Toucan's Bill*/, 9664/*Capuchin Leash*/ };
    /* @note the Ringmaster's rings (wiki). Ring of Range is looked up by name, it's newer than my item list */
    constexpr short RINGS[] = { 1932/*Fistful Of Rings*/, 1904/*The One Ring*/, 1986/*Gemini Ring*/, 1874/*Ring Of Force*/, 1876/*Ring Of Winds*/,
                                1996/*Ring of Wisdom*/, 2970/*Ring Of Water*/, 3174/*Ring Of Smithing*/, 6028/*Ring Of Shrinking*/, 6846/*Ring of Nature*/,
                                8962/*Ring of Night*/ };

    constexpr int QUEST_TICKETS = 10;  // @note wiki: 10 Golden Tickets start a Ring Quest
    constexpr int QUEST_STEPS   = 10;  // @note wiki: 10 tasks
    constexpr int QUEST_LEVEL   = 20;  // @note wiki: level 20 or higher
    constexpr int RING_TRADE    = 5;   // @note wiki: five of the same ring for another random ring
    constexpr int FUNTIME_WINS  = 50;  // @note wiki: a Funtime Block for winning 50 games
    constexpr int MYSTERY_CHANCE = 50; // @note my pick: 1 in 50 wins also gives a Mystery Prize

    long long now_ms()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    ::peer *peer_of(ENetPeer *peer) { return static_cast<::peer*>(peer->data); }
    int tile_index(int x, int y) { return cord(x, y); }

    ::pos center_tile(const ::peer &peer)
    {
        return ::pos{ std::floor((peer.pos.x + 16.0f) / 32.0f), std::floor((peer.pos.y + 16.0f) / 32.0f) };
    }
    ::world *world_of(const ::peer &peer)
    {
        if (peer.netid == 0) return nullptr;
        auto it = std::ranges::find(worlds, peer.recent_worlds.back(), &::world::name);
        return (it != worlds.end()) ? &*it : nullptr;
    }
    ENetPeer *in_world(const std::string &world, int uid)
    {
        ENetPeer *found = nullptr;
        peers(world, PeerSearch::WORLD, [&](ENetPeer *p) { if (peer_of(p)->user_id == uid) found = p; });
        return found;
    }
    void dialog(ENetPeer *peer, const std::string &text) { send_varlist(peer, { "OnDialogRequest", text }); }
    void bubble(ENetPeer *peer, const std::string &text) { send_varlist(peer, { "OnTalkBubble", peer_of(peer)->netid, text, 0u, 1u }); }
    void overlay(ENetPeer *peer, const std::string &text) { send_varlist(peer, { "OnTextOverlay", text }); }
    void world_message(const std::string &world, const std::string &text)
    {
        peers(world, PeerSearch::WORLD, [&text](ENetPeer *p) { on::ConsoleMessage(p, text); });
    }
    bool wearing(const ::peer &peer, short id)
    {
        return std::ranges::find(peer.clothing, static_cast<float>(id)) != peer.clothing.end();
    }
    void save_carnival(::peer &peer) { if (!peer.growid.empty()) peer.mysql_update("carnival", carnival_serialize(peer)); }

    /* ---------------- when the Carnival is in town ---------------- */

    int mode_override = -1; // @note -1 auto, 0 off, 1 on. kept in carnival.txt next to the server
    bool mode_loaded = false;

    void load_mode()
    {
        if (mode_loaded) return;
        mode_loaded = true;
        std::ifstream file("carnival.txt");
        std::string word{};
        if (file >> word) mode_override = (word == "on") ? 1 : (word == "off") ? 0 : -1;
    }
    void save_mode()
    {
        std::ofstream("carnival.txt") << ((mode_override == 1) ? "on" : (mode_override == 0) ? "off" : "auto");
    }
    /* @return the day of the month the Carnival starts in this month: the first Friday (UTC) */
    int first_friday(const std::tm &utc)
    {
        const int weekday_of_1st = (utc.tm_wday - (utc.tm_mday - 1) % 7 + 7) % 7; // @note 0 Sunday .. 6 Saturday
        return 1 + (5 - weekday_of_1st + 7) % 7;
    }
    std::tm utc_now()
    {
        const std::time_t t = std::time(nullptr);
        std::tm utc{};
#ifdef _WIN32
        gmtime_s(&utc, &t);
#else
        gmtime_r(&t, &utc);
#endif
        return utc;
    }
    bool auto_open()
    {
        const std::tm utc = utc_now();
        const int friday = first_friday(utc);
        return utc.tm_mday >= friday && utc.tm_mday <= friday + 2; // @note wiki: a 3-day event starting on a Friday each month (my pick: the first Friday)
    }

    /* ---------------- the CARNIVAL world ---------------- */

    enum game : u_char { SHOOTING, GULCH, MIRROR, CONCENTRATION, BOUNCE, RACE, SPIKY, WASTE };

    struct room_def
    {
        game kind;
        const char *name;
        int tent_x, tent_y;
        int x0, y0, x1, y1; // @note the room's walls (inclusive)
        int start_x, start_y;
        std::array<short, 3> prizes; // @note Common, Rare, Ultimate (wiki)
        int seconds;      // @note my pick: time limit
        int min_players;  // @note wiki: Brutal Bounce needs 2, Death Race 5000 needs 4 ("at least three other players")
    };
    /*
    * @note my own layout: every game is its own striped building up in the night sky, standing on stilts over a walkway.
    *   three walkways (y 21, 44 and the ground at 57) are joined by Carnival Ladders. each building's Carnival Tent stands
    *   on the walkway right under it. the plaza on the ground has the main door, the Ringmaster's stage and both booths.
    */
    constexpr int WALK_TOP = 21, WALK_MID = 44, GROUND = 57;
    const room_def ROOMS[] = {
        { GULCH,         "Growganoth Gulch", 10, GROUND - 1,   0,  22, 8,  53, 1,  52, {1884/*Red Pennant*/, 1880/*Carnival Awning*/, 1936/*Blazing Electro Wings*/}, 60, 1 },
        { RACE,          "Death Race 5000",  3,  WALK_TOP - 1, 1,  3,  39, 17, 2,  16, {2238/*Carnival Pipe*/, 2220/*Tiny Tank*/, 2216/*Edvoid's Fire-Nado*/}, 120, 4 },
        { MIRROR,        "Mirror Maze",      12, WALK_MID - 1, 10, 22, 27, 40, 11, 39, {1882/*Carnival Block*/, 1872/*Funhouse Mirror*/, 1942/*Calliope*/}, 60, 1 },
        { CONCENTRATION, "Concentration",    44, WALK_TOP - 1, 42, 9,  60, 17, 43, 16, {1888/*Green Pennant*/, 1928/*Ringmaster Hat*/, 1944/*Diamond Diaper*/}, 60, 1 },
        { SHOOTING,      "Shooting Gallery", 16, GROUND - 1,   12, 45, 30, 53, 21, 52, {1890/*Yellow Pennant*/, 1894/*Lion Block*/, 1892/*Lion Taming Whip*/}, 45, 1 },
        { SHOOTING,      "Shooting Gallery", 65, WALK_TOP - 1, 63, 9,  81, 17, 72, 16, {1886/*Blue Pennant*/, 1896/*Elephant Block*/, 1934/*Orbs Of Elixir*/}, 45, 1 },
        { CONCENTRATION, "Concentration",    83, GROUND - 1,   68, 45, 86, 53, 69, 52, {1914/*Carnival Platform*/, 1906/*Carnival Sign*/, 1938/*Diamond Wings*/}, 60, 1 },
        { BOUNCE,        "Brutal Bounce",    32, WALK_MID - 1, 30, 25, 68, 40, 49, 39, {2568/*Carnival Spikeball*/, 2570/*Carny Vest*/, 2572/*Flame Scythe*/}, 120, 2 },
        { SPIKY,         "Spiky Survivor",   72, WALK_MID - 1, 70, 22, 89, 40, 79, 23, {7178/*Carnival Ladders*/, 7176/*Dragon Block*/, 7136/*Ethereal Rainbow Dragon*/}, 60, 1 },
        { GULCH,         "Growganoth Gulch", 88, GROUND - 1,   91, 22, 99, 53, 92, 52, {1878/*Carnival Wallpaper*/, 1930/*Ringmaster Suit*/, 1940/*Diamond Rocket Shoes*/}, 60, 1 },
        { WASTE,         "Waste a Ticket",   98, 0,  0,  0,  0,  0,  0,  0,  {0, 0, 0}, 0, 1 } // @note up in the top right corner: only floating gets you there
    };
    constexpr std::size_t ROOM_COUNT = sizeof(ROOMS) / sizeof(ROOMS[0]);
    constexpr int SHOOTING_GOAL = 30; // @note wiki: a score of 30 wins
    constexpr int SHOOTING_TARGETS = 3; // @note my pick: Bullseyes up at once
    constexpr int CARD_COUNT = 16;
    constexpr int RING_X = 53, BOOTH_LEFT_X = 46, BOOTH_RIGHT_X = 56, DOOR_X = 50;
    constexpr short CARNIVAL_LADDERS = 7178;
    constexpr short NIGHT = 2; // @note base weather: the night sky (Weather Machine - Night)

    ::pos landing_of(const room_def &room) { return ::pos{ (room.tent_x >= 97) ? room.tent_x - 1 : room.tent_x + 1, room.tent_y }; }
    bool inside(const room_def &room, int x, int y) { return x > room.x0 && x < room.x1 && y > room.y0 && y < room.y1; }
    ::pos card_tile(const room_def &room, int i) { return ::pos{ room.x0 + 2 + (i % 8) * 2, (i < 8) ? room.y0 + 3 : room.y0 + 5 }; }
    ::pos race_gate(const room_def &room) { return ::pos{ room.x0 + 4, 0 }; } // @note x of the gate column

    std::string tent_label(const room_def &room) { return std::format("{} - 1 Golden Ticket", room.name); }

    void put(::world &world, int x, int y, short fg, short bg = -1)
    {
        if (x < 0 || x >= 100 || y < 0 || y >= 60) return;
        ::block &b = world.blocks[tile_index(x, y)];
        b.fg = fg;
        if (bg >= 0) b.bg = bg;
    }
    void sign(::world &world, int x, int y, short id, const std::string &label)
    {
        put(world, x, y, id);
        world.signs.emplace_back(label, ::pos{x, y});
    }

    void build_room(::world &world, const room_def &room)
    {
        if (room.kind == WASTE) return;
        for (int y = room.y0; y <= room.y1; ++y)
            for (int x = room.x0; x <= room.x1; ++x)
            {
                const bool wall = (x == room.x0 || x == room.x1 || y == room.y0 || y == room.y1);
                put(world, x, y, wall ? CARNIVAL_BLOCK : 0, (room.kind == MIRROR) ? FUNHOUSE_MIRROR : CARNIVAL_WALLPAPER);
            }
        switch (room.kind)
        {
            case CONCENTRATION:
                for (int i = 0; i < CARD_COUNT; ++i) { const ::pos c = card_tile(room, i); put(world, c.x_int(), c.y_int(), CARD_BACK); }
                break;
            case MIRROR: // @note stacked corridors 2 tall, a hole in each ceiling (other side each time) with a step under it, low walls to hop
            {
                for (int k = 0; k < 5; ++k)
                {
                    const int floor_y = room.y1 - 3 - 3 * k, low_y = room.y1 - 1 - 3 * k;
                    const bool right = (k % 2 == 0);
                    for (int x = room.x0 + 1; x < room.x1; ++x)
                        if (right ? x < room.x1 - 2 : x > room.x0 + 2) put(world, x, floor_y, MIRROR_MAZE_BLOCK);
                    put(world, right ? room.x1 - 2 : room.x0 + 2, low_y, MIRROR_MAZE_BLOCK); // @note the step
                    for (int x : { room.x0 + 5 + k % 2, room.x0 + 9 + k % 2, room.x0 + 13 - k % 2 }) put(world, x, low_y, MIRROR_MAZE_BLOCK);
                }
                const int top_low = room.y1 - 1 - 3 * 5;
                for (int x : { room.x0 + 7, room.x0 + 11 }) put(world, x, top_low, MIRROR_MAZE_BLOCK);
                put(world, room.x0 + 1, top_low, BULLSEYE);
                break;
            }
            case GULCH: // @note platforms zig-zag up a tower; Evil Eyes in the walls; the Bullseye at the top
            {
                int row = 0;
                for (int y = room.y1 - 2; y >= room.y0 + 3; y -= 2, ++row) // @note 2 apart: a normal jump makes it
                {
                    const bool top = (y - 2 < room.y0 + 3);
                    const int from = top ? room.x0 + 1 : (row % 2 == 0) ? room.x0 + 1 : room.x0 + 4;
                    const int to = top ? room.x1 - 1 : (row % 2 == 0) ? room.x0 + 4 : room.x1 - 1;
                    for (int x = from; x <= to; ++x) put(world, x, y, CREEPSTONE);
                }
                for (int y = room.y1 - 4; y > room.y0 + 2; y -= 4)
                    put(world, ((y / 4) % 2 == 0) ? room.x0 : room.x1, y, EVIL_EYE);
                put(world, (room.x0 + room.x1) / 2, room.y0 + 1, BULLSEYE);
                break;
            }
            case RACE: // @note a starting pen behind a gate, then death spikes, lava and walls to the finish
            {
                const int floor = room.y1 - 1, o = room.x0 - 11; // @note the course was drawn for x0 = 11
                for (int y = room.y0 + 1; y < room.y1; ++y) put(world, race_gate(room).x_int(), y, CARNIVAL_BLOCK);
                for (int x : { 18, 23, 33, 41, 46 }) put(world, x + o, floor, DEATH_SPIKES);
                for (int x : { 27, 28, 37, 38, 39 }) put(world, x + o, floor, LAVA);
                for (int x : { 20, 30, 44 }) { put(world, x + o, floor, CARNIVAL_BLOCK); put(world, x + o, floor - 1, CARNIVAL_BLOCK); }
                for (int x = 25; x <= 29; ++x) put(world, x + o, floor - 4, DEATH_SPIKES); // @note jump too high over the lava and you hit these
                for (int x = 32; x <= 40; ++x) put(world, x + o, floor - 2, CARNIVAL_PLATFORM); // @note a ledge over the lava
                for (int x = 35; x <= 39; ++x) put(world, x + o, room.y0 + 1, DEATH_SPIKES);
                put(world, room.x1 - 1, floor, BULLSEYE); // @note the finish line
                break;
            }
            case BOUNCE:
            {
                const int ox = room.x0 - 11, oy = room.y1 - 42; // @note drawn for x0 = 11, y1 = 42
                for (int x = 18; x <= 24; ++x) put(world, x + ox, 37 + oy, CARNIVAL_PLATFORM);
                for (int x = 36; x <= 42; ++x) put(world, x + ox, 37 + oy, CARNIVAL_PLATFORM);
                for (int x = 27; x <= 33; ++x) put(world, x + ox, 33 + oy, CARNIVAL_PLATFORM);
                break;
            }
            case SPIKY: // @note floating platforms over a floor of Carnival Spikeballs
            {
                for (int x = room.x0 + 1; x < room.x1; ++x) put(world, x, room.y1 - 1, CARNIVAL_SPIKEBALL);
                for (int y = room.y1 - 5; y >= room.y0 + 4; y -= 4)
                    for (int x = room.x0 + 1; x < room.x1; ++x)
                        if ((x + y) % 4 != 0) put(world, x, y, CREEP_PLATFORM);
                break;
            }
            default: break;
        }
    }

    void build(::world &world)
    {
        world.blocks.assign(cord(0, 60), ::block{0, 0});
        world.doors.clear(); world.signs.clear(); world.trees.clear(); world.random_blocks.clear(); world.displays.clear();
        world.area_locks.clear(); world.vendings.clear(); world.storages.clear(); world.providers.clear(); world.mailboxes.clear();
        world.donation_boxes.clear(); world.shelves.clear(); world.heart_monitors.clear(); world.mannequins.clear();
        world.weather_settings.clear(); world.magplants.clear(); world.fish_tanks.clear(); world.fish_mounts.clear();
        world.lobster_traps.clear(); world.auto_machines.clear(); world.timed_machines.clear(); world.krankens.clear();
        world.vaults.clear(); world.game_tiles.clear(); world.game_generators.clear(); world.challenge_timers.clear(); world.ovens.clear();
        world.weather = ::pos{-1, -1};
        world.base_weather = NIGHT;
        world.xenonite = 0;
        world.is_public = false;

        auto backdrop = [&](int x, int y, short bg) { if (x >= 0 && x < 100 && y >= 0 && y < 60) world.blocks[tile_index(x, y)].bg = bg; };
        auto empty = [&](int x, int y) { return x >= 0 && x < 100 && y >= 0 && y < 60 && world.blocks[tile_index(x, y)].fg == 0; };

        /* @note the ground and the two walkways up in the air */
        for (int x = 0; x < 100; ++x)
        {
            put(world, x, GROUND, CARNIVAL_BLOCK);
            for (int y = GROUND + 1; y < 60; ++y) put(world, x, y, BEDROCK);
        }
        for (int x = 0; x <= 90; ++x) put(world, x, WALK_TOP, CARNIVAL_PLATFORM);
        for (int x = 9; x <= 90; ++x) put(world, x, WALK_MID, CARNIVAL_PLATFORM);
        for (int x = 94; x < 100; ++x) put(world, x, 1, CARNIVAL_PLATFORM); // @note the plank for Waste a Ticket

        for (const room_def &room : ROOMS) build_room(world, room);

        for (const room_def &room : ROOMS)
        {
            if (room.kind == WASTE) continue;
            for (int x = room.x0; x <= room.x1; x += 2) // @note pennants along the roof
                if (empty(x, room.y0 - 1)) put(world, x, room.y0 - 1, PENNANTS[(x / 2) % 4]);
            for (int x = room.x0; x <= room.x1; ++x) // @note striped stilts down to the walkway
            {
                if (x != room.x0 && x != room.x1 && (x - room.x0) % 6 != 0) continue;
                for (int y = room.y1 + 1; y <= room.tent_y; ++y) backdrop(x, y, CARNIVAL_PIPE);
            }
        }

        /* @note ladders: ground to the middle walkway, middle to the top */
        for (int x : { 36, 64 })
            for (int y = WALK_MID - 1; y < GROUND; ++y) put(world, x, y, CARNIVAL_LADDERS);
        for (int x : { 29, 69 })
            for (int y = WALK_TOP - 1; y < WALK_MID; ++y) put(world, x, y, CARNIVAL_LADDERS);

        /* @note the plaza: the Ringmaster's stage under a striped awning, the booths either side, the main door in the middle */
        for (int x = 42; x <= 60; ++x)
        {
            put(world, x, 50, CARNIVAL_AWNING);
            if (x % 2 == 0) put(world, x, 49, PENNANTS[(x / 2) % 4]);
            for (int y = 51; y < GROUND; ++y) backdrop(x, y, (x == 42 || x == 60) ? CARNIVAL_PIPE : CARNIVAL_WALLPAPER);
        }
        for (int y = 53; y < GROUND; ++y) /* @note the Hall of Mirrors around the main door: walk past and look funny */
            for (int x = DOOR_X - 3; x <= DOOR_X + 2; ++x) backdrop(x, y, FUNHOUSE_MIRROR);

        put(world, DOOR_X, GROUND - 1, MAIN_DOOR);
        put(world, DOOR_X, GROUND, BEDROCK);
        world.doors.emplace_back("EXIT", "", "", ::pos{DOOR_X, GROUND - 1});
        world.spawn = ::pos{DOOR_X, GROUND - 1}.by_32(false);

        put(world, RING_X, GROUND - 1, THE_RINGMASTER);
        sign(world, RING_X, GROUND - 3, CARNIVAL_SIGN, "The Ringmaster's Quest For The Ring");
        for (int x : { BOOTH_LEFT_X, BOOTH_RIGHT_X })
        {
            put(world, x, GROUND - 1, GOLDEN_TICKET_BOOTH);
            sign(world, x, GROUND - 3, CARNIVAL_SIGN, "Golden Tickets! 100 rarity of any items, or 30 per World Lock");
        }
        for (const auto &[x, y] : { std::pair{40, GROUND - 1}, {62, GROUND - 1}, {5, GROUND - 1}, {95, GROUND - 1},
                                    {41, WALK_TOP - 1}, {62, WALK_TOP - 1}, {86, WALK_TOP - 1}, {20, WALK_MID - 1}, {50, WALK_MID - 1}, {80, WALK_MID - 1} })
            if (empty(x, y)) put(world, x, y, CALLIOPE);

        for (const room_def &room : ROOMS)
        {
            put(world, room.tent_x, room.tent_y, CARNIVAL_TENT);
            world.doors.emplace_back(room.name, "", "", ::pos{room.tent_x, room.tent_y});
            const ::pos land = landing_of(room);
            sign(world, land.x_int(), land.y_int(), CARNIVAL_LANDING, room.name);
            if (room.tent_y >= 2) sign(world, room.tent_x, room.tent_y - 2, CARNIVAL_SIGN, tent_label(room));
        }
    }

    /* ---------------- running games ---------------- */

    struct saved_tile { ::pos pos{}; short fg{}; u_char flags{}; long long at{}; }; // @note at = when it goes back (0 = when the game ends)

    struct room_state
    {
        int phase{};             // @note 0 nobody, 1 waiting for players / counting down, 2 playing
        long long first_in{}, starts{}, ends{}, next_event{};
        std::vector<int> players{}; // @note user ids still in the game
        int starters{};          // @note how many began (Brutal Bounce, Spiky Survivor: last one standing wins)
        int score{};
        std::vector<short> cards{};
        int first{-1}, second{-1};
        long long flip_back{};
        std::vector<saved_tile> saved{};
        std::vector<std::pair<::pos, long long>> targets{}; // @note Shooting Gallery: Bullseyes up and when they move
    };
    std::unordered_map<std::string, std::array<room_state, ROOM_COUNT>> states{};
    std::unordered_map<int, std::pair<std::string, int>> playing{}; // @note user id -> world, room index

    room_state &state_of(const std::string &world, std::size_t room) { return states[world][room]; }

    /* @brief change a tile for a while (or until the game ends), remembering what it was */
    void swap_tile(::world &world, room_state &st, const ::pos &pos, short fg, long long until, int flags = -1)
    {
        if (!valid_tile(world, pos)) return;
        ::block &b = world.blocks[tile_index(pos.x_int(), pos.y_int())];
        auto it = std::ranges::find(st.saved, pos, &saved_tile::pos);
        if (it == st.saved.end()) st.saved.push_back({ pos, b.fg, b.state[2], until });
        else it->at = until;
        b.fg = fg;
        if (flags >= 0) b.state[2] = static_cast<u_char>(flags);
        world_tile_update(world, pos);
    }
    void restore_tile(::world &world, const saved_tile &t)
    {
        ::block &b = world.blocks[tile_index(t.pos.x_int(), t.pos.y_int())];
        b.fg = t.fg, b.state[2] = t.flags, b.hits[0] = 0;
        world_tile_update(world, t.pos);
    }
    void restore_all(::world &world, room_state &st)
    {
        for (const saved_tile &t : st.saved) restore_tile(world, t);
        st.saved.clear();
    }

    void teleport(ENetPeer *p, ::world &world, const ::pos &tile)
    {
        ::peer *pp = peer_of(p);
        pp->pos = tile.by_32(false);
        peers(world.name, PeerSearch::WORLD, [&](ENetPeer *o) { send_varlist(o, { "OnSetPos", CL_Vec2f{pp->pos.x, pp->pos.y} }, pp->netid); });
        send_varlist(p, { "OnZoomCamera", 10000.0f, 1000u });
        send_varlist(p, { "OnSetFreezeState", 0u }, pp->netid);
    }

    std::size_t room_at_tent(const ::pos &tile)
    {
        for (std::size_t i = 0; i < ROOM_COUNT; ++i)
            if (ROOMS[i].tent_x == tile.x_int() && ROOMS[i].tent_y == tile.y_int()) return i;
        return ROOM_COUNT;
    }

    void give_prize(ENetPeer *p, ::world &world, const room_def &room)
    {
        ::peer *pp = peer_of(p);
        ENetEvent event{ .peer = p };
        const int roll = RandomRange(0, 100);
        const int tier = (roll < 3) ? 2 : (roll < 20) ? 1 : 0; // @note my pick: Common 80%, Rare 17%, Ultimate 3%
        const short prize = room.prizes[tier];
        give_or_drop(event, world, prize, 1);
        for (int i = 0; i <= tier; ++i) fireworks(event, pp->pos); // @note wiki: 1 firework Common, 2 Rare, 3 Ultimate
        static const char *TIERS[] = { "`2Common``", "`9Rare``", "`5ULTIMATE``" };
        overlay(p, std::format("`2You win!`` {} prize: `w{}``", TIERS[tier], id_to_item(prize).raw_name));
        world_message(world.name, std::format("`5[``{} won a {} prize at {}: `w{}```5]``", pp->display_growid, TIERS[tier], room.name, id_to_item(prize).raw_name));
        play_sfx(p, "audio/fanfare.wav");

        if (RandomRange(0, MYSTERY_CHANCE) == 0)
        {
            const short mystery = MYSTERY_PRIZES[RandomRange(0, static_cast<int>(std::size(MYSTERY_PRIZES)))];
            give_or_drop(event, world, mystery, 1);
            on::ConsoleMessage(p, std::format("`9A Mystery Prize! You also got a `w{}``!``", id_to_item(mystery).raw_name));
        }
        if (++pp->carnival.wins == FUNTIME_WINS)
        {
            give_or_drop(event, world, FUNTIME_BLOCK, 1);
            on::ConsoleMessage(p, "`9You've won 50 Carnival games! Here's a `wFuntime Block``!``");
        }
        save_carnival(*pp);
        ring_progress(p, RT_CARNIVAL_WINS, 1);
    }

    /* @brief a player is done with a game: out to the landing, with a prize if they won */
    void finish_player(::world &world, std::size_t index, int uid, bool won, bool move_out = true)
    {
        room_state &st = state_of(world.name, index);
        std::erase(st.players, uid);
        playing.erase(uid);
        ENetPeer *p = in_world(world.name, uid);
        if (p == nullptr) return;
        ::peer *pp = peer_of(p);
        const room_def &room = ROOMS[index];
        pp->rest_pos = world.spawn;
        send_varlist(p, { "OnSetMissionTimer", 0u, 0 });
        if (move_out) teleport(p, world, landing_of(room));
        else pp->rest_pos = landing_of(room).by_32(false); // @note they died: the respawn takes them out
        if (won) give_prize(p, world, room);
        else
        {
            overlay(p, "`4You lose!`` Better luck next time!");
            play_sfx(p, "audio/bleep_fail.wav");
        }
    }

    void end_room(::world &world, std::size_t index)
    {
        room_state &st = state_of(world.name, index);
        for (int uid : std::vector<int>(st.players)) finish_player(world, index, uid, false);
        restore_all(world, st);
        for (const auto &[pos, at] : st.targets)
            if (valid_tile(world, pos) && world.blocks[tile_index(pos.x_int(), pos.y_int())].fg == BULLSEYE)
            {
                world.blocks[tile_index(pos.x_int(), pos.y_int())].fg = 0;
                world_tile_update(world, pos);
            }
        if (ROOMS[index].kind == CONCENTRATION)
            for (int i = 0; i < CARD_COUNT; ++i)
            {
                const ::pos c = card_tile(ROOMS[index], i);
                world.blocks[tile_index(c.x_int(), c.y_int())].fg = CARD_BACK;
                world_tile_update(world, c);
            }
        st = room_state{};
    }

    /* @brief Shooting Gallery: put a Bullseye somewhere free */
    void spawn_target(::world &world, room_state &st, const room_def &room, long long now)
    {
        for (int tries = 0; tries < 30; ++tries)
        {
            const int x = RandomRange(room.x0 + 2, room.x1 - 1), y = RandomRange(room.y0 + 1, room.y1 - 3);
            ::block &b = world.blocks[tile_index(x, y)];
            if (b.fg != 0) continue;
            b.fg = BULLSEYE;
            world_tile_update(world, ::pos{x, y});
            st.targets.emplace_back(::pos{x, y}, now + RandomRange(2000, 3500)); // @note my pick: a Bullseye moves after 2-3.5 s
            return;
        }
    }
    void clear_target(::world &world, room_state &st, const ::pos &pos)
    {
        std::erase_if(st.targets, [&](const auto &t) { return t.first == pos; });
        world.blocks[tile_index(pos.x_int(), pos.y_int())].fg = 0;
        world_tile_update(world, pos);
    }

    void start_game(::world &world, std::size_t index, long long now)
    {
        room_state &st = state_of(world.name, index);
        const room_def &room = ROOMS[index];
        st.phase = 2;
        st.ends = now + room.seconds * 1000ll;
        st.starters = static_cast<int>(st.players.size());
        st.next_event = now + 500;
        switch (room.kind)
        {
            case SHOOTING:
                for (int i = 0; i < SHOOTING_TARGETS; ++i) spawn_target(world, st, room, now);
                break;
            case CONCENTRATION:
            {
                st.cards.clear();
                for (int i = 0; i < CARD_COUNT; ++i) st.cards.push_back(CARD_FACES[i % 4]);
                for (int i = CARD_COUNT - 1; i > 0; --i) std::swap(st.cards[i], st.cards[RandomRange(0, i + 1)]);
                break;
            }
            case RACE:
                for (int y = room.y0 + 1; y < room.y1; ++y) swap_tile(world, st, ::pos{race_gate(room).x_int(), y}, 0, 0);
                break;
            default: break;
        }
        static const char *HOW[] = {
            "Punch the Bullseyes! Score 30 to win!", "Climb to the top and punch the Bullseye!", "Find your way through the maze to the Bullseye!",
            "Find the matching cards!", "Punch to throw Spikeballs! Last one standing wins!", "Race to the finish!", "Don't fall! Punch the air to blow your rivals off!", ""
        };
        for (int uid : st.players)
            if (ENetPeer *p = in_world(world.name, uid); p != nullptr)
            {
                send_varlist(p, { "OnSetMissionTimer", static_cast<u_int>(room.seconds), 1, room.name });
                overlay(p, std::format("`wGo!`` {}", HOW[room.kind]));
                play_sfx(p, "audio/race_start.wav");
            }
    }

    /* @brief someone walked into a tent with a ticket: into the game room */
    void enter_room(ENetPeer *p, ::world &world, std::size_t index)
    {
        ::peer *pp = peer_of(p);
        room_state &st = state_of(world.name, index);
        const room_def &room = ROOMS[index];
        const long long now = now_ms();
        st.players.push_back(pp->user_id);
        playing[pp->user_id] = { world.name, static_cast<int>(index) };

        ::pos start{room.start_x, room.start_y};
        if (room.kind == BOUNCE) start = ::pos{ room.x0 + 3 + (static_cast<int>(st.players.size()) * 7) % (room.x1 - room.x0 - 5), room.start_y };
        if (room.kind == SPIKY) start = ::pos{ room.x0 + 3 + (static_cast<int>(st.players.size()) * 5) % (room.x1 - room.x0 - 5), room.start_y };
        pp->rest_pos = start.by_32(false); // @note dying in the Death Race starts you over in the pen
        teleport(p, world, start);
        play_in_world(world.name, pp->netid, "audio/door_open.wav");

        if (room.min_players <= 1 && room.kind != SPIKY) { start_game(world, index, now); return; }

        if (st.phase == 0) st.phase = 1, st.first_in = now;
        const int need = room.min_players - static_cast<int>(st.players.size());
        if (room.kind == SPIKY && st.starts == 0) st.starts = now + 15'000; // @note my pick: 15 s for others to join
        else if (need <= 0 && st.starts == 0) st.starts = now + 10'000; // @note my pick: a 10 s countdown once there are enough
        for (int uid : st.players)
            if (ENetPeer *o = in_world(world.name, uid); o != nullptr)
                overlay(o, (st.starts != 0) ? std::format("`w{}`` starts in `w{}`` seconds!", room.name, (st.starts - now + 999) / 1000)
                                            : std::format("Waiting for `w{}`` more player{}...", need, (need == 1) ? "" : "s"));
    }

    /* @brief Brutal Bounce / Spiky Survivor: is someone the last one standing? */
    void check_last_standing(::world &world, std::size_t index)
    {
        room_state &st = state_of(world.name, index);
        if (st.phase != 2) return;
        if (st.players.empty()) { end_room(world, index); return; }
        if (st.starters >= 2 && st.players.size() == 1)
        {
            finish_player(world, index, st.players.front(), true);
            end_room(world, index);
        }
    }

    void time_up(::world &world, std::size_t index)
    {
        room_state &st = state_of(world.name, index);
        const room_def &room = ROOMS[index];
        if (room.kind == SPIKY) // @note everyone still up there survived
            for (int uid : std::vector<int>(st.players)) finish_player(world, index, uid, true);
        for (int uid : st.players)
            if (ENetPeer *p = in_world(world.name, uid); p != nullptr) on::ConsoleMessage(p, "`4Time's up!``");
        end_room(world, index);
    }

    void game_punch(ENetEvent &event, ::world &world, std::size_t index, const ::pos &tile, long long now)
    {
        room_state &st = state_of(world.name, index);
        const room_def &room = ROOMS[index];
        ::peer *pp = peer_of(event.peer);
        if (st.phase != 2 || !inside(room, tile.x_int(), tile.y_int())) return;
        ::block &b = world.blocks[tile_index(tile.x_int(), tile.y_int())];
        switch (room.kind)
        {
            case SHOOTING:
                if (b.fg != BULLSEYE) return;
                clear_target(world, st, tile);
                ++st.score;
                play_sfx(event.peer, "audio/pinata_lasso.wav");
                bubble(event.peer, std::format("`w{}``/{}", st.score, SHOOTING_GOAL));
                if (st.score >= SHOOTING_GOAL) { finish_player(world, index, pp->user_id, true); end_room(world, index); return; }
                spawn_target(world, st, room, now);
                break;
            case GULCH: case MIRROR:
                if (b.fg != BULLSEYE) return;
                finish_player(world, index, pp->user_id, true);
                end_room(world, index);
                break;
            case CONCENTRATION:
            {
                int card = -1;
                for (int i = 0; i < CARD_COUNT; ++i) if (card_tile(room, i) == tile) card = i;
                if (card < 0 || b.fg != CARD_BACK || st.second >= 0 || st.cards.size() != CARD_COUNT) return;
                b.fg = st.cards[card];
                world_tile_update(world, tile);
                play_sfx(event.peer, "audio/page_turn.wav");
                if (st.first < 0) { st.first = card; return; }
                if (st.cards[st.first] == st.cards[card]) // @note a match stays face up
                {
                    st.first = -1;
                    play_sfx(event.peer, "audio/success.wav");
                    bool all = true;
                    for (int i = 0; i < CARD_COUNT; ++i)
                    {
                        const ::pos c = card_tile(room, i);
                        if (world.blocks[tile_index(c.x_int(), c.y_int())].fg == CARD_BACK) all = false;
                    }
                    if (all) { finish_player(world, index, pp->user_id, true); end_room(world, index); }
                }
                else st.second = card, st.flip_back = now + 900; // @note my pick: a wrong pair shows for 0.9 s
                break;
            }
            case BOUNCE: case SPIKY:
            {
                if (b.fg != 0) return;
                if (room.kind == BOUNCE) swap_tile(world, st, tile, CARNIVAL_SPIKEBALL, now + 2000); // @note my pick: a thrown Spikeball lasts 2 s
                else swap_tile(world, st, tile, WINTER_WIND, now + 2000, pp->facing_left ? S_LEFT : S_RIGHT);
                break;
            }
            default: break;
        }
    }

    void room_tick(::world &world, std::size_t index, long long now)
    {
        room_state &st = state_of(world.name, index);
        const room_def &room = ROOMS[index];

        for (std::size_t i = 0; i < st.saved.size();)
        {
            if (st.saved[i].at != 0 && now >= st.saved[i].at) { restore_tile(world, st.saved[i]); st.saved.erase(st.saved.begin() + i); }
            else ++i;
        }
        if (st.phase == 0) return;
        if (st.phase == 1)
        {
            if (st.players.empty()) { end_room(world, index); return; }
            if (st.starts != 0 && now >= st.starts)
            {
                if (static_cast<int>(st.players.size()) >= room.min_players) start_game(world, index, now);
                else st.starts = 0;
            }
            else if (st.starts == 0 && now - st.first_in > 90'000) // @note my pick: not enough people came in 90 s: tickets back
            {
                for (int uid : std::vector<int>(st.players))
                    if (ENetPeer *p = in_world(world.name, uid); p != nullptr)
                    {
                        ENetEvent event{ .peer = p };
                        give_or_drop(event, world, GOLDEN_TICKET, 1);
                        on::ConsoleMessage(p, std::format("Not enough players came for `w{}``. Here's your Golden Ticket back.", room.name));
                    }
                end_room(world, index);
            }
            return;
        }
        if (now >= st.ends) { time_up(world, index); return; }

        if (st.flip_back != 0 && now >= st.flip_back) // @note Concentration: turn a wrong pair back over
        {
            for (int card : { st.first, st.second })
                if (card >= 0)
                {
                    const ::pos c = card_tile(room, card);
                    world.blocks[tile_index(c.x_int(), c.y_int())].fg = CARD_BACK;
                    world_tile_update(world, c);
                }
            st.first = st.second = -1, st.flip_back = 0;
        }
        if (room.kind == SHOOTING)
            for (std::size_t i = 0; i < st.targets.size(); ++i)
                if (now >= st.targets[i].second)
                {
                    const ::pos old = st.targets[i].first;
                    clear_target(world, st, old);
                    spawn_target(world, st, room, now);
                    break;
                }
        if (now < st.next_event) return;
        switch (room.kind)
        {
            case GULCH: // @note my pick: a platform tile switches off for 2 s, an Evil Eye opens for 1.5 s
            {
                st.next_event = now + 700;
                const int x = RandomRange(room.x0 + 1, room.x1), y = RandomRange(room.y0 + 3, room.y1 - 1);
                if (world.blocks[tile_index(x, y)].fg == CREEPSTONE && y != room.y1 - 2) swap_tile(world, st, ::pos{x, y}, 0, now + 2000);
                if (RandomRange(0, 2) == 0)
                    for (int ey = room.y1 - 4; ey > room.y0 + 2; ey -= 4)
                    {
                        const int ex = ((ey / 4) % 2 == 0) ? room.x0 : room.x1;
                        if (RandomRange(0, 3) != 0) continue;
                        const u_char flags = world.blocks[tile_index(ex, ey)].state[2];
                        swap_tile(world, st, ::pos{ex, ey}, EVIL_EYE, now + 1500, flags | S_TOGGLE);
                    }
                break;
            }
            case SPIKY: // @note my pick: every 0.4 s a platform turns off for 3 s
            {
                st.next_event = now + 400;
                const short off = (id_to_item(CREEP_PLATFORM_OFF).collision == collision::NO_COLLISION) ? CREEP_PLATFORM_OFF : 0;
                for (int tries = 0; tries < 10; ++tries)
                {
                    const int x = RandomRange(room.x0 + 1, room.x1), y = RandomRange(room.y0 + 1, room.y1 - 1);
                    if (world.blocks[tile_index(x, y)].fg != CREEP_PLATFORM) continue;
                    swap_tile(world, st, ::pos{x, y}, off, now + 3000);
                    break;
                }
                break;
            }
            default: st.next_event = now + 1000; break;
        }
    }

    /* ---------------- Golden Ticket Booth ---------------- */

    int ticket_price(const ::peer &peer) { return wearing(peer, RING_OF_SAVINGS) ? 90 : 100; }  // @note wiki: Ring of Savings saves 10%
    int tickets_per_lock(const ::peer &peer) { return wearing(peer, RING_OF_SAVINGS) ? 33 : 30; }

    void booth_dialog(ENetPeer *p)
    {
        const ::peer *pp = peer_of(p);
        dialog(p, ::create_dialog()
            .set_default_color("`o")
            .add_label_with_icon("big", "`wGolden Ticket Booth``", GOLDEN_TICKET_BOOTH)
            .add_spacer("small")
            .add_textbox(std::format("Golden Tickets cost {} Rarity worth of any items. If you give me extras, I'll keep a record. I'll give you {} Tickets per World Lock.",
                ticket_price(*pp), tickets_per_lock(*pp)))
            .add_textbox(std::format("You currently have a credit of `w{}`` Rarity.", pp->carnival.credit))
            .add_spacer("small")
            .add_item_picker("itemid", "`wGive items``", "Choose the items to trade for Golden Tickets!")
            .end_dialog("carnival_booth", "Goodbye", ""));
    }

    /* ---------------- The Ringmaster ---------------- */

    std::vector<short> ring_pool()
    {
        std::vector<short> pool{};
        for (short id : RINGS)
            if (id_to_item(id).raw_name.find("Ring") != std::string::npos) pool.push_back(id);
        static short range = -1; // @note Ring of Range: newer than the list above, so it's found by name
        if (range < 0)
        {
            range = 0;
            for (const ::item &item : items) if (item.raw_name == "Ring of Range" || item.raw_name == "Ring Of Range") { range = static_cast<short>(item.id); break; }
        }
        if (range > 0) pool.push_back(range);
        return pool;
    }

    /* @return a block (not a seed) that's easy to farm, for "bring me ..." */
    short random_delivery()
    {
        for (int tries = 0; tries < 200; ++tries)
        {
            const int id = RandomRange(2, std::min<int>(static_cast<int>(items.size()), 4000));
            if (id % 2 != 0) continue;
            const ::item &item = id_to_item(static_cast<u_short>(id));
            if (item.type != type::FOREGROUND || item.rarity < 1 || item.rarity > 25 || (item.cat & CAT_UNTRADEABLE)) continue;
            if (id_to_item(static_cast<u_short>(id + 1)).type != type::SEED) continue; // @note it has a tree, so it can be farmed
            return static_cast<short>(id);
        }
        return 2; // @note Dirt
    }

    void new_task(::peer &peer)
    {
        static const int TASKS[] = { RT_DELIVER_ITEM, RT_HARVEST_RARITY, RT_PLANT_RARITY, RT_BREAK, RT_BREAK_RARITY, RT_XP, RT_GEMS, RT_WORLD_LOCKS,
                                     RT_CARNIVAL_WINS, RT_PROVIDERS, RT_GEIGER, RT_FISH_LBS, RT_FOSSILS };
        auto &c = peer.carnival;
        c.task = TASKS[RandomRange(0, static_cast<int>(std::size(TASKS)))];
        c.target = 0, c.progress = 0, c.told = false;
        auto hundreds = [](int lo, int hi) { return RandomRange(lo / 100, hi / 100 + 1) * 100; };
        switch (c.task) // @note my pick: how much each task asks for
        {
            case RT_DELIVER_ITEM:
                c.target = random_delivery();
                c.goal = std::clamp(2000 / std::max<int>(id_to_item(static_cast<u_short>(c.target)).rarity, 1), 20, 200);
                break;
            case RT_HARVEST_RARITY: case RT_PLANT_RARITY: case RT_BREAK_RARITY: c.goal = hundreds(2000, 5000); break;
            case RT_BREAK:          c.goal = hundreds(300, 1000); break;
            case RT_XP:             c.goal = hundreds(500, 2500); break;
            case RT_GEMS:           c.goal = hundreds(2000, 10000); break;
            case RT_WORLD_LOCKS:    c.goal = RandomRange(1, 3); break;
            case RT_CARNIVAL_WINS:  c.goal = RandomRange(2, 6); break;
            case RT_PROVIDERS:      c.goal = RandomRange(3, 11) * 10; break;
            case RT_GEIGER:         c.goal = RandomRange(1, 3); break;
            case RT_FISH_LBS:       c.goal = RandomRange(1, 7) * 50; break;
            case RT_FOSSILS:        c.goal = 1; break;
        }
    }

    std::string task_text(const ::peer &peer)
    {
        const auto &c = peer.carnival;
        switch (c.task)
        {
            case RT_DELIVER_ITEM:   return std::format("Your task is to bring me `w{} {}``!", c.goal, id_to_item(static_cast<u_short>(c.target)).raw_name);
            case RT_HARVEST_RARITY: return std::format("Your task is to harvest `w{}`` rarity worth of trees!", c.goal);
            case RT_PLANT_RARITY:   return std::format("Your task is to plant `w{}`` rarity worth of seeds!", c.goal);
            case RT_BREAK:          return std::format("Your task is to smash `w{}`` blocks! Any blocks will do.", c.goal);
            case RT_BREAK_RARITY:   return std::format("Your task is to smash `w{}`` rarity worth of blocks!", c.goal);
            case RT_XP:             return std::format("Your task is to earn `w{}`` XP! Any way you like.", c.goal);
            case RT_GEMS:           return std::format("Your task is to bring me `w{}`` Gems!", c.goal);
            case RT_WORLD_LOCKS:    return std::format("Your task is to bring me `w{}`` World Lock{}!", c.goal, (c.goal == 1) ? "" : "s");
            case RT_CARNIVAL_WINS:  return std::format("Your task is to win `w{}`` Carnival games!", c.goal);
            case RT_PROVIDERS:      return std::format("Your task is to collect from `w{}`` providers, like Chickens, Cows and Coffee Makers!", c.goal);
            case RT_GEIGER:         return std::format("Your task is to find `w{}`` radioactive item{} with a Geiger Counter!", c.goal, (c.goal == 1) ? "" : "s");
            case RT_FISH_LBS:       return std::format("Your task is to catch `w{}`` lbs. of fish!", c.goal);
            case RT_FOSSILS:        return std::format("Your task is to prepare `w{}`` fossil{} in a Fossil Prep Station!", c.goal, (c.goal == 1) ? "" : "s");
        }
        return "";
    }
    bool is_delivery(int task) { return task == RT_DELIVER_ITEM || task == RT_GEMS || task == RT_WORLD_LOCKS; }

    long long delivery_have(const ::peer &peer)
    {
        ::peer &p = const_cast<::peer&>(peer);
        switch (peer.carnival.task)
        {
            case RT_DELIVER_ITEM: return p.have_slot(::slot(static_cast<short>(peer.carnival.target), 0));
            case RT_GEMS: return peer.gems;
            case RT_WORLD_LOCKS: return p.have_slot(::slot(WORLD_LOCK, 0));
        }
        return 0;
    }

    /* @return the ring this player has RING_TRADE of, or 0 */
    short tradeable_ring(::peer &peer)
    {
        for (short ring : ring_pool()) if (peer.have_slot(::slot(ring, 0)) >= RING_TRADE) return ring;
        return 0;
    }

    /* @param from_goals opened from Goals & Quests in the wrench menu: only shows the quest, handing in is done at the Ringmaster */
    void ringmaster_dialog(ENetPeer *p, bool from_goals = false)
    {
        ::peer *pp = peer_of(p);
        const auto &c = pp->carnival;
        ::create_dialog d{};
        d.set_default_color("`o");
        if (from_goals)
        {
            d.add_label_with_icon("big", "`9Quest For The Ring``", THE_RINGMASTER);
            if (c.step <= 0)
                d.add_textbox("You're not on the Ringmaster's quest. When the Carnival is in town, visit `wCARNIVAL`` and give the Ringmaster 10 Golden Tickets to start it.");
            else
            {
                const long long have = is_delivery(c.task) ? std::min<long long>(delivery_have(*pp), c.goal) : std::min<long long>(c.progress, c.goal);
                d.add_smalltext(std::format("(Step {}/{})", c.step, QUEST_STEPS))
                 .add_spacer("small")
                 .add_textbox(task_text(*pp))
                 .add_smalltext(std::format("({} {}/{})", is_delivery(c.task) ? "You have" : "Current progress", have, c.goal))
                 .add_spacer("small")
                 .add_textbox(have >= c.goal ? "`2Done!`` Go back to the Ringmaster at the Carnival for your next task."
                                             : "When it's done, go back to the Ringmaster at the Carnival.");
            }
            d.add_spacer("small")
             .add_textbox(carnival_open() ? "`2The Carnival is in town!`` Go to `wCARNIVAL``." : "`4The Carnival isn't in town right now.`` It comes for 3 days from the first Friday of each month.")
             .add_button("goals", "Back");
            dialog(p, d.end_dialog("popup", "Close", ""));
            return;
        }
        if (c.step <= 0)
        {
            d.add_label_with_icon("big", "`wThe Ringmaster``", THE_RINGMASTER)
             .add_spacer("small")
             .add_textbox("Come one, come all, to the most extraordinary show in Growtopia! I am the Ringmaster. That means I know a lot about Rings! "
                          "For 10 Golden Tickets, I might even tell you how you get a Ring of your own...")
             .add_spacer("small")
             .add_button("start_quest", "`9Give 10 Golden Tickets``");
        }
        else
        {
            const bool done = c.progress >= c.goal;
            d.add_label_with_icon("big", "`9Quest For The Ring``", THE_RINGMASTER)
             .add_smalltext(std::format("(Step {}/{})", c.step, QUEST_STEPS))
             .add_spacer("small")
             .add_textbox(task_text(*pp))
             .add_spacer("small");
            if (is_delivery(c.task))
            {
                const long long have = delivery_have(*pp);
                d.add_smalltext(std::format("(You have {}/{})", std::min<long long>(have, c.goal), c.goal));
                if (have >= c.goal)
                    d.add_button("deliver", (c.task == RT_DELIVER_ITEM) ? std::format("`9Deliver {} {}``", c.goal, id_to_item(static_cast<u_short>(c.target)).raw_name)
                                          : (c.task == RT_GEMS) ? std::format("`9Deliver {} Gems``", c.goal) : std::format("`9Deliver {} World Lock{}``", c.goal, (c.goal == 1) ? "" : "s"));
            }
            else
            {
                d.add_smalltext(std::format("(Current progress {}/{})", std::min<long long>(c.progress, c.goal), c.goal));
                if (done) d.add_button("next_step", (c.step >= QUEST_STEPS) ? "`9I did it! Give me my Ring!``" : "`9I did it! What's next?``");
            }
            d.add_button("give_up", "`4Give up this quest``");
        }
        d.add_spacer("small")
         .add_smalltext(std::format("If you had {} rings of the same type maybe we could have made a deal...", RING_TRADE));
        if (const short ring = tradeable_ring(*pp); ring != 0)
            d.add_button("trade_rings", std::format("`9Trade {} {}``", RING_TRADE, id_to_item(ring).raw_name));
        dialog(p, d.end_dialog("carnival_ring", "Goodbye", ""));
    }

    /* @return the ring given, 0 if none */
    short give_ring(ENetEvent &event, ::world *world, short not_this = 0)
    {
        ::peer *pp = peer_of(event.peer);
        std::vector<short> pool = ring_pool();
        if (not_this != 0 && pool.size() > 1) std::erase(pool, not_this);
        if (pool.empty()) return 0;
        const short ring = pool[RandomRange(0, static_cast<int>(pool.size()))];
        if (world != nullptr) give_or_drop(event, *world, ring, 1);
        else modify_item_inventory(event, ::slot(ring, 1));
        overlay(event.peer, std::format("`9The Ringmaster gives you a `w{}``!", id_to_item(ring).raw_name));
        if (world != nullptr)
        {
            for (int i = 0; i < 3; ++i) fireworks(event, pp->pos);
            world_message(world->name, std::format("`5[``{} got a `w{}`` from the Ringmaster!`5]``", pp->display_growid, id_to_item(ring).raw_name));
        }
        play_sfx(event.peer, "audio/fanfare.wav");
        return ring;
    }

    /* @brief put the new ring on straight away (if it made it into the backpack) */
    void wear_ring(ENetEvent &event, short ring)
    {
        ::peer *pp = peer_of(event.peer);
        const ::item &item = id_to_item(ring);
        if (ring == 0 || item.cloth_type >= pp->clothing.size() || pp->have_slot(::slot(ring, 0)) == 0) return;
        float &worn = pp->clothing[item.cloth_type];
        if (worn == ring) return;
        if (worn != 0) playmod_message(event.peer, static_cast<u_short>(worn), false);
        worn = static_cast<float>(ring);
        playmod_message(event.peer, static_cast<u_short>(ring), true);
        pp->update_effects();
        if (u_short punch_id = get_punch_id(static_cast<u_int>(ring)); punch_id != 0) pp->punch_effect = punch_id;
        send_varlist(event.peer, { "OnEquipNewItem", ring }, pp->netid);
        on::SetClothing(event.peer, true);
    }

    void advance(ENetEvent &event, ::world *world)
    {
        ::peer *pp = peer_of(event.peer);
        auto &c = pp->carnival;
        if (c.step >= QUEST_STEPS)
        {
            c = { .credit = c.credit, .wins = c.wins };
            const short ring = give_ring(event, world);
            wear_ring(event, ring); // @note karim: the finished quest puts the ring on you and you cheer
            if (pp->netid != 0) on::Action(event, "/cheer");
        }
        else
        {
            ++c.step;
            new_task(*pp);
            play_sfx(event.peer, "audio/success.wav");
        }
        save_carnival(*pp);
        pp->mysql_update_all();
        if (c.step > 0) ringmaster_dialog(event.peer);
    }
}

/* ---------------- shared ---------------- */

bool is_carnival_world(const std::string &name)
{
    if (!name.starts_with("CARNIVAL")) return false;
    const std::string rest = name.substr(8);
    if (rest.empty()) return true;
    if (rest.length() > 2 || rest.front() == '0' || !std::ranges::all_of(rest, [](unsigned char c) { return std::isdigit(c); })) return false;
    const int n = atoi(rest.c_str());
    return n >= 1 && n <= 50; // @note wiki: CARNIVAL1 to CARNIVAL50
}

bool carnival_open()
{
    load_mode();
    return (mode_override >= 0) ? mode_override == 1 : auto_open();
}

void carnival_prepare(::world &world)
{
    if (!is_carnival_world(world.name) || world.visitors > 0 || world.owner != 0) return; // @note a world someone locked is theirs
    states.erase(world.name);
    build(world); // @note always fresh for its first visitor: nothing from a game or a staff edit is left over
}

bool carnival_tile_change(ENetEvent &event, ::world &world, ::gamePacket &gamePacket)
{
    if (!is_carnival_world(world.name) || world.owner != 0) return false;
    ::peer *pPeer = peer_of(event.peer);
    const ::pos tile = gamePacket.punch;
    const short fg = world.blocks[tile_index(tile.x_int(), tile.y_int())].fg;

    if (auto in = playing.find(pPeer->user_id); in != playing.end() && in->second.first == world.name && gamePacket.id == 18)
    {
        game_punch(event, world, static_cast<std::size_t>(in->second.second), tile, now_ms());
        return true;
    }
    if (gamePacket.id == 18 || gamePacket.id == 32)
    {
        if (fg == THE_RINGMASTER || fg == GOLDEN_TICKET_BOOTH)
        {
            if (!carnival_open()) throw std::runtime_error("The Carnival has left town! Come back when it returns.");
            if (fg == GOLDEN_TICKET_BOOTH) booth_dialog(event.peer);
            else ringmaster_dialog(event.peer);
            return true;
        }
    }
    if (pPeer->role >= MODERATOR && !pPeer->player_mode) return false; // @note staff can still fix things
    if (gamePacket.id == 18 || gamePacket.id == 32) return true; // @note punching the scenery does nothing
    throw std::runtime_error("You can't build at the Carnival!");
}

bool carnival_activate(ENetEvent &event, ::world &world, const ::pos &tile)
{
    if (!is_carnival_world(world.name) || world.owner != 0) return false;
    if (world.blocks[tile_index(tile.x_int(), tile.y_int())].fg != CARNIVAL_TENT) return false;
    const std::size_t index = room_at_tent(tile);
    if (index >= ROOM_COUNT) return false;
    ::peer *pPeer = peer_of(event.peer);
    const room_def &room = ROOMS[index];
    auto refuse = [&](const std::string &why)
    {
        bubble(event.peer, why);
        send_varlist(event.peer, { "OnSetFreezeState", 0u }, pPeer->netid);
        play_sfx(event.peer, "audio/punch_locked.wav");
        return true;
    };
    if (!carnival_open()) return refuse("The Carnival has left town! Come back when it returns.");
    if (playing.contains(pPeer->user_id)) return true;
    room_state &st = state_of(world.name, index);
    if (room.kind != WASTE)
    {
        if (room.min_players <= 1 && room.kind != SPIKY && st.phase != 0) return refuse("Someone is already playing! Wait your turn.");
        if (st.phase == 2) return refuse("A game is already going. Wait for the next one!");
    }
    if (pPeer->have_slot(::slot(GOLDEN_TICKET, 0)) <= 0) return refuse("This game costs 1 Golden Ticket.");
    modify_item_inventory(event, ::slot(GOLDEN_TICKET, -1));

    if (room.kind == WASTE)
    {
        overlay(event.peer, "`9You wasted a ticket!`` Was it worth it? (yes)");
        play_sfx(event.peer, "audio/fanfare.wav");
        fireworks(event, pPeer->pos);
        send_varlist(event.peer, { "OnSetFreezeState", 0u }, pPeer->netid);
        return true;
    }
    enter_room(event.peer, world, index);
    return true;
}

void carnival_moved(ENetEvent &event)
{
    ::peer *pPeer = peer_of(event.peer);
    auto in = playing.find(pPeer->user_id);
    if (in == playing.end()) return;
    ::world *world = world_of(*pPeer);
    if (world == nullptr || world->name != in->second.first) return;
    const std::size_t index = static_cast<std::size_t>(in->second.second);
    const room_def &room = ROOMS[index];
    if (room.kind != RACE || state_of(world->name, index).phase != 2) return;
    if (center_tile(*pPeer).x_int() >= room.x1 - 2) // @note first past the finish line
    {
        room_state &st = state_of(world->name, index);
        finish_player(*world, index, pPeer->user_id, true);
        for (int uid : std::vector<int>(st.players))
            if (ENetPeer *p = in_world(world->name, uid); p != nullptr) on::ConsoleMessage(p, std::format("`w{}`` won the race!", pPeer->growid));
        end_room(*world, index);
    }
}

void carnival_respawn(ENetEvent &event)
{
    ::peer *pPeer = peer_of(event.peer);
    auto in = playing.find(pPeer->user_id);
    if (in == playing.end()) return;
    ::world *world = world_of(*pPeer);
    if (world == nullptr || world->name != in->second.first) { playing.erase(in); return; }
    const std::size_t index = static_cast<std::size_t>(in->second.second);
    const room_def &room = ROOMS[index];
    room_state &st = state_of(world->name, index);
    if (room.kind == RACE || st.phase != 2) return; // @note the race (and waiting) starts you over in the pen
    finish_player(*world, index, pPeer->user_id, false, false);
    if (room.kind == BOUNCE || room.kind == SPIKY) check_last_standing(*world, index);
    else end_room(*world, index);
}

void carnival_left_world(ENetEvent &event, ::world &world)
{
    ::peer *pPeer = peer_of(event.peer);
    auto in = playing.find(pPeer->user_id);
    if (in != playing.end() && in->second.first == world.name)
    {
        const std::size_t index = static_cast<std::size_t>(in->second.second);
        room_state &st = state_of(world.name, index);
        std::erase(st.players, pPeer->user_id);
        playing.erase(in);
        pPeer->rest_pos = world.spawn;
        send_varlist(event.peer, { "OnSetMissionTimer", 0u, 0 });
        const game kind = ROOMS[index].kind;
        if (st.players.empty() || (st.phase == 2 && kind != BOUNCE && kind != SPIKY && kind != RACE)) end_room(world, index);
        else if (kind == BOUNCE || kind == SPIKY) check_last_standing(world, index);
    }
    if (is_carnival_world(world.name) && world.visitors <= 1) // @note the last one out: put every room back before the world is saved
    {
        if (auto s = states.find(world.name); s != states.end())
        {
            for (std::size_t i = 0; i < ROOM_COUNT; ++i) end_room(world, i);
            states.erase(world.name);
        }
    }
}

void carnival_tick()
{
    static long long last{};
    const long long now = now_ms();
    if (now - last < 100) return;
    last = now;
    for (auto &[name, rooms] : states)
    {
        auto world = std::ranges::find(worlds, name, &::world::name);
        if (world == worlds.end()) continue;
        for (std::size_t i = 0; i < ROOM_COUNT; ++i) room_tick(*world, i, now);
    }
}

/* ---------------- Goals & Quests ---------------- */

std::string carnival_goal_line(const ::peer &peer)
{
    const auto &c = peer.carnival;
    if (c.step <= 0) return "Not started. Visit the Ringmaster at the Carnival.";
    const long long have = is_delivery(c.task) ? std::min<long long>(delivery_have(peer), c.goal) : std::min<long long>(c.progress, c.goal);
    return std::format("Step {}/{}: {} ({}/{})", c.step, QUEST_STEPS, have >= c.goal ? "`2done, see the Ringmaster``" : "in progress", have, c.goal);
}

void carnival_goal_dialog(ENetPeer *peer) { ringmaster_dialog(peer, true); }

/* ---------------- Ringmaster tasks ---------------- */

void ring_progress(ENetPeer *peer, int task, long long amount)
{
    if (peer == nullptr || amount <= 0) return;
    ::peer *pp = peer_of(peer);
    if (pp == nullptr || !pp->authenticated) return;
    auto &c = pp->carnival;
    if (c.step <= 0 || c.task != task || is_delivery(task) || c.progress >= c.goal) return;
    c.progress = std::min<long long>(c.progress + amount, c.goal);
    if (c.progress >= c.goal && !c.told)
    {
        c.told = true;
        on::ConsoleMessage(peer, std::format("`9Ringmaster's quest step {}/{} done!`` Go back to the Ringmaster at the Carnival.", c.step, QUEST_STEPS));
        play_sfx(peer, "audio/success.wav");
        save_carnival(*pp);
    }
}

std::string carnival_serialize(const ::peer &peer)
{
    const auto &c = peer.carnival;
    return std::format("{},{},{},{},{},{},{}", c.credit, c.wins, c.step, c.task, c.target, c.goal, c.progress);
}

void carnival_load(::peer &peer, const std::string &text)
{
    const std::vector<std::string> parts = readch(text, ',');
    auto num = [&](std::size_t i) { return (i < parts.size()) ? atoll(parts[i].c_str()) : 0ll; };
    auto &c = peer.carnival;
    c.credit = static_cast<int>(std::max(num(0), 0ll));
    c.wins = static_cast<int>(std::max(num(1), 0ll));
    c.step = static_cast<int>(std::clamp(num(2), 0ll, static_cast<long long>(QUEST_STEPS)));
    c.task = static_cast<int>(num(3));
    c.target = static_cast<int>(num(4));
    c.goal = static_cast<int>(std::max(num(5), 1ll));
    c.progress = std::max(num(6), 0ll);
    c.told = c.progress >= c.goal;
    if (c.step > 0 && (c.task <= RT_NONE || c.task >= RT_COUNT)) c.step = 0; // @note a damaged save
}

/* ---------------- dialogs ---------------- */

void carnival_booth_edit(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = peer_of(event.peer);
    ::world *world = world_of(*pPeer);
    if (world == nullptr || !is_carnival_world(world->name) || !carnival_open()) return;

    const short id = static_cast<short>(atoi(hPipe["itemid"].c_str()));
    if (id <= 0) return;
    const ::item &item = id_to_item(id);
    const bool lock = (id == WORLD_LOCK || id == DIAMOND_LOCK);
    if (id == GOLDEN_TICKET) throw std::runtime_error("Those are already Golden Tickets!");
    if ((item.cat & CAT_UNTRADEABLE) || item.type == type::FIST || item.type == type::WRENCH || (!lock && (item.rarity <= 0 || item.rarity >= 999)))
        throw std::runtime_error("I can't take that.");
    const int have = pPeer->have_slot(::slot(id, 0));
    if (have <= 0) return;

    if (hPipe["count"].empty()) // @note step 1: how many?
    {
        dialog(event.peer, ::create_dialog()
            .set_default_color("`o")
            .add_label_with_icon("big", std::format("`wTrade {}``", item.raw_name), item.id)
            .add_textbox(lock ? std::format("Each {} is worth `w{}`` Golden Tickets.", item.raw_name, tickets_per_lock(*pPeer) * ((id == DIAMOND_LOCK) ? 100 : 1))
                              : std::format("Each {} is worth `w{}`` Rarity. {} Rarity buys a Golden Ticket.", item.raw_name, item.rarity, ticket_price(*pPeer)))
            .add_text_input("count", "How many?", static_cast<short>(have), 3)
            .embed_data("itemid", id)
            .end_dialog("carnival_booth", "Cancel", "Trade"));
        return;
    }
    const int count = std::min(atoi(hPipe["count"].c_str()), have);
    if (count <= 0) return;
    long long tickets{}, credit = pPeer->carnival.credit;
    if (lock) tickets = static_cast<long long>(count) * tickets_per_lock(*pPeer) * ((id == DIAMOND_LOCK) ? 100 : 1);
    else
    {
        credit += static_cast<long long>(item.rarity) * count;
        tickets = credit / ticket_price(*pPeer);
        credit %= ticket_price(*pPeer);
    }
    if (tickets > 0 && !pPeer->can_hold(GOLDEN_TICKET, static_cast<int>(tickets)))
        throw std::runtime_error("You don't have room for that many Golden Tickets!");

    modify_item_inventory(event, ::slot(id, static_cast<short>(-count)));
    pPeer->carnival.credit = static_cast<int>(credit);
    if (tickets > 0) modify_item_inventory(event, ::slot(GOLDEN_TICKET, static_cast<short>(tickets)));
    on::ConsoleMessage(event.peer, std::format("`7[``You traded {} {} for `w{}`` Golden Ticket{}. Credit left: `w{}`` Rarity.`7]``",
        count, item.raw_name, tickets, (tickets == 1) ? "" : "s", credit));
    if (tickets > 0) play_sfx(event.peer, "audio/cash_register.wav");
    save_carnival(*pPeer);
    pPeer->mysql_update_all();
    booth_dialog(event.peer);
}

void carnival_ring_edit(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = peer_of(event.peer);
    ::world *world = world_of(*pPeer);
    if (world == nullptr || !is_carnival_world(world->name) || !carnival_open()) return;
    auto &c = pPeer->carnival;
    const std::string button = hPipe["buttonClicked"];

    if (button == "start_quest" && c.step <= 0)
    {
        if (static_cast<int>(pPeer->level.front()) < QUEST_LEVEL) throw std::runtime_error(std::format("Come back when you're level {}!", QUEST_LEVEL));
        if (pPeer->have_slot(::slot(GOLDEN_TICKET, 0)) < QUEST_TICKETS) throw std::runtime_error("That'll be 10 Golden Tickets, please.");
        modify_item_inventory(event, ::slot(GOLDEN_TICKET, -QUEST_TICKETS));
        c.step = 1;
        new_task(*pPeer);
        save_carnival(*pPeer);
        pPeer->mysql_update_all();
        dialog(event.peer, ::create_dialog()
            .set_default_color("`o")
            .add_label_with_icon("big", "`9Quest For The Ring``", THE_RINGMASTER)
            .add_textbox("You don't get to pick which one, mind you. If you want one, picked randomly at my discretion, you'll need to complete 10 little tasks for me.")
            .add_textbox("There's no hurry. If the Carnival leaves town before you're done, finish up when we come back next month!")
            .add_button("show_quest", "`9What's my first task?``")
            .end_dialog("carnival_ring", "Goodbye", ""));
        return;
    }
    if (button == "show_quest") { ringmaster_dialog(event.peer); return; }
    if (button == "give_up" && c.step > 0)
    {
        dialog(event.peer, ::create_dialog()
            .set_default_color("`o")
            .add_label_with_icon("big", "`4Give up?``", THE_RINGMASTER)
            .add_textbox("You can quit at any time, but if you do, you'll lose all your progress, and you'll have to pay another 10 Golden Tickets to start again!")
            .add_button("give_up_sure", "`4Yes, I give up``")
            .end_dialog("carnival_ring", "Keep going", ""));
        return;
    }
    if (button == "give_up_sure" && c.step > 0)
    {
        c = { .credit = c.credit, .wins = c.wins };
        save_carnival(*pPeer);
        on::ConsoleMessage(event.peer, "You gave up the Ringmaster's quest.");
        return;
    }
    if (button == "deliver" && c.step > 0 && is_delivery(c.task))
    {
        if (delivery_have(*pPeer) < c.goal) throw std::runtime_error("You don't have enough yet!");
        if (c.task == RT_GEMS)
        {
            pPeer->gems -= c.goal;
            on::SetBux(event);
        }
        else modify_item_inventory(event, ::slot(static_cast<short>((c.task == RT_WORLD_LOCKS) ? WORLD_LOCK : c.target), static_cast<short>(-c.goal)));
        c.progress = c.goal;
        advance(event, world);
        return;
    }
    if (button == "next_step" && c.step > 0 && !is_delivery(c.task) && c.progress >= c.goal)
    {
        advance(event, world);
        return;
    }
    if (button == "trade_rings")
    {
        const short ring = tradeable_ring(*pPeer);
        if (ring == 0) return;
        modify_item_inventory(event, ::slot(ring, -RING_TRADE));
        give_ring(event, world, ring);
        pPeer->mysql_update_all();
    }
}

void carnival_command(ENetEvent &event, const std::string_view text)
{
    ::peer *pPeer = peer_of(event.peer);
    std::string arg{text.substr(std::min(text.find(' '), text.size()))};
    std::erase(arg, ' ');
    for (char &ch : arg) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    load_mode();
    if (!arg.empty() && pPeer->role >= DEVELOPER)
    {
        if (arg == "on") mode_override = 1;
        else if (arg == "off") mode_override = 0;
        else if (arg == "auto") mode_override = -1;
        else { on::ConsoleMessage(event.peer, "Usage: /carnival on|off|auto"); return; }
        save_mode();
    }
    const std::tm utc = utc_now();
    const int friday = first_friday(utc);
    std::string when = (mode_override == 1) ? "(switched on by staff)" : (mode_override == 0) ? "(switched off by staff)"
                     : std::format("(it's here for 3 days from the first Friday of each month: this month the {}th to the {}th, UTC)", friday, friday + 2);
    on::ConsoleMessage(event.peer, std::format("{} {}", carnival_open() ? "`2The Carnival is in town!`` Go to `wCARNIVAL``." : "`4The Carnival isn't in town.``", when));
}
