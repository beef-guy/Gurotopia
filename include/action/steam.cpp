#include "pch.hpp"

#include "onVariant/ConsoleMessage.hpp"
#include "tools/create_dialog.hpp"
#include "tools/random.hpp"
#include "action/ghosts.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <unordered_map>

#include "steam.hpp"

namespace
{
    constexpr short GEAR = 3258, TUBES = 3260, PISTON = 3262, LIFTER_PISTON = 3264, BELLOWS = 3266, RISER_BELLOWS = 3268,
                    VALVE = 3280, ORGAN = 3282, VENT = 3284, DOOR = 3286, COLLECTOR = 3412, SCRAMBLER = 3414, PIPE = 3592,
                    FUNNEL = 3752, FUNNEL_UP = 3754, FUNNEL_DOWN = 3756, CROSSOVER = 3766, CRANK = 3768, LAMP = 3770, SPIKES = 3772;

    /* @note directions, the same order the game uses: 0 right, 1 down, 2 left, 3 up. +1 turns right, +3 turns left */
    constexpr int DX[4] = { 1, 0, -1, 0 };
    constexpr int DY[4] = { 0, 1, 0, -1 };
    constexpr u_char back(u_char d) { return static_cast<u_char>((d + 2) & 3); }

    /* @note NET_GAME_PACKET_STEAM byte 3: the kinds that start steam on a tile, read from the game's handler (it turns them into 0 right .. 3 up) */
    constexpr u_char START_EVENT[4] = { 18/*right*/, 0/*down*/, 19/*left*/, 16/*up*/ };
    constexpr u_char EV_VENT_HOT   = 3; // @note a Steam Vent burns for 3 seconds
    constexpr u_char EV_DOOR_OPEN  = 4; // @note sets the Steam Door's tile flag 0x40
    constexpr u_char EV_DOOR_SHUT  = 5;

    constexpr int MAX_STEPS = 120;          // @note a puff runs out after this many tiles
    constexpr std::size_t MAX_PUFFS = 300;  // @note per world
    constexpr long long STEP_MS = 100;      // @note the game moves steam one tile every 100 ms
    constexpr long long COLLECT_MS = 20000; // @note "held there for up to 20 seconds"
    constexpr int ENGINE_HOT = 500;         // @note "heat this above 500 degrees"
    constexpr int FUEL_HEAT = 100;          // @note one Nuclear Fuel: +100 degrees
    constexpr int ENGINE_MAX = 5000;
    constexpr long long COOL_SECONDS = 6;   // @note our own guess: an engine loses 1 degree every 6 seconds (10 a minute)
    constexpr long long ENGINE_PUFF_MS = 1000;

    long long now_ms()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    short fg_at(const ::world &world, int x, int y)
    {
        if (!valid_tile(world, ::pos{ x, y })) return -1;
        return world.blocks[cord(x, y)].fg;
    }

    ::block *block_at(::world &world, int x, int y)
    {
        if (!valid_tile(world, ::pos{ x, y })) return nullptr;
        return &world.blocks[cord(x, y)];
    }

    void send_steam(const ::world &world, int x, int y, u_char kind)
    {
        const ::blob blob = compress_state(::gamePacket{
            .type = static_cast<int>(0x20u | (u_int(kind) << 24)), // @note NET_GAME_PACKET_STEAM
            .punch = ::pos{ x, y } // @note ints at 0x2c and 0x30: the tile
        });
        peers(world.name, PeerSearch::WORLD, [&blob](ENetPeer *peer) { send_data(*peer, blob); });
    }

    /* @return which way a funnel lets steam out */
    u_char funnel_exit(short id, u_char flags)
    {
        if (id == FUNNEL_UP) return 3;
        if (id == FUNNEL_DOWN) return 1;
        return (flags & S_LEFT) ? 2 : 0; // @note the plain funnel points the way it was placed
    }

    /* @return true if steam moving `d` can go into this tile */
    bool takes(const ::world &world, int x, int y, u_char d)
    {
        const short id = fg_at(world, x, y);
        if (id <= 0) return false;
        const u_char flags = world.blocks[cord(x, y)].state[2];
        switch (id)
        {
            case GEAR: case TUBES: case PISTON: case LIFTER_PISTON: case BELLOWS: case RISER_BELLOWS: case PIPE:
            case VALVE: case ORGAN: case VENT: case COLLECTOR: case SCRAMBLER: case CROSSOVER:
            case DOOR: case STEAM_LAUNCHER: case SPIRIT_STORAGE_UNIT:
                return true;
            case FUNNEL: case FUNNEL_UP: case FUNNEL_DOWN:
                return back(d) != funnel_exit(id, flags); // @note "can enter this Funnel from 3 sides"
            case CRANK:
                return (flags & S_TOGGLE) ? (d == 0 || d == 2) : (d == 1 || d == 3); // @note tile flag 0x40: turned sideways
            case LAMP: case SPIKES:
                return d == 3; // @note "It can only be powered from below"
        }
        return false;
    }

    bool takes_from(const ::world &world, int x, int y, u_char d) { return takes(world, x + DX[d], y + DY[d], d); }

    void start_steam(::world &world, int x, int y, u_char d)
    {
        send_steam(world, x, y, START_EVENT[d & 3]);
        if (world.steam_puffs.size() < MAX_PUFFS)
            world.steam_puffs.push_back(::steam_puff{ .x = static_cast<short>(x), .y = static_cast<short>(y), .dir = static_cast<u_char>(d & 3) });
    }

    /* @brief one puff moves one tile. @return the puffs it turns into (none: it's done) */
    void step(::world &world, const ::steam_puff &puff, std::vector<::steam_puff> &out, long long now)
    {
        if (puff.steps >= MAX_STEPS) return;
        const u_char d = puff.dir;
        const int x = puff.x + DX[d], y = puff.y + DY[d];
        if (!takes(world, x, y, d)) return; // @note nothing to carry it: it puffs out
        ::block &block = world.blocks[cord(x, y)];
        const short id = block.fg;
        const auto go = [&](u_char to) { out.push_back(::steam_puff{ .x = static_cast<short>(x), .y = static_cast<short>(y), .dir = to, .steps = static_cast<u_char>(puff.steps + 1) }); };
        const auto first_open = [&](std::initializer_list<u_char> order) {
            for (const u_char to : order) if (takes_from(world, x, y, to)) { go(to); return; }
        };

        switch (id)
        {
            case SPIRIT_STORAGE_UNIT:
                ssu_steamed(world, ::pos{ x, y }); // @note "provide it with Steam power to store the ghosts"
                return;
            case DOOR:
                block.state[2] ^= S_TOGGLE; // @note open or shut, even without access to the world
                send_steam(world, x, y, (block.state[2] & S_TOGGLE) ? EV_DOOR_OPEN : EV_DOOR_SHUT);
                return;
            case STEAM_LAUNCHER:
                if (!(block.state[2] & S_TOGGLE))
                {
                    block.state[2] |= S_TOGGLE; // @note charged: a steam trampoline now
                    world_tile_update(world, ::pos{ x, y });
                }
                return;
            case LAMP:
                return; // @note the game lights it for 10 seconds itself
            case SPIKES:
                block.state[2] ^= S_TOGGLE; // @note "Power it again to lower the spikes"
                return;
            case CROSSOVER: // @note "one runs from left to down, and the other runs from up to right"
            {
                static constexpr u_char through[4] = { 1/*came going right -> down*/, 0/*going down -> right*/, 3/*going left -> up*/, 2/*going up -> left*/ };
                if (takes_from(world, x, y, through[d])) go(through[d]);
                return;
            }
            case CRANK:
                if (takes_from(world, x, y, d)) go(d); // @note straight through, the way it's turned
                return;
            case FUNNEL: case FUNNEL_UP: case FUNNEL_DOWN:
            {
                const u_char to = funnel_exit(id, block.state[2]);
                if (takes_from(world, x, y, to)) go(to);
                return;
            }
            case VALVE:
            {
                if (d == 1 || d == 3) // @note "If steam enters this block from the top or bottom, it will go out the bigger side, and the valve will flip"
                {
                    const u_char big = (block.state[2] & S_LEFT) ? 2 : 0;
                    block.state[2] ^= S_LEFT;
                    first_open({ big, back(big) });
                    return;
                }
                break;
            }
            case SCRAMBLER: // @note "it will exit in a random legal direction"
            {
                std::vector<u_char> ways{};
                for (u_char to = 0; to < 4; ++to) if (to != back(d) && takes_from(world, x, y, to)) ways.push_back(to);
                if (!ways.empty()) go(ways[RandomRange(0, static_cast<int>(ways.size()) - 1)]);
                return;
            }
            case COLLECTOR: // @note holds steam until it has one for every way out (but the one the last steam came from), then shoots them all
            {
                auto c = std::ranges::find(world.steam_collectors, ::pos{ x, y }, &::steam_collector::pos);
                if (c == world.steam_collectors.end()) c = world.steam_collectors.insert(world.steam_collectors.end(), ::steam_collector{ .pos = ::pos{ x, y } });
                c->from = back(d);
                c->until = now + COLLECT_MS;
                ++c->held;
                std::vector<u_char> ways{};
                for (u_char to = 0; to < 4; ++to) if (to != c->from && takes_from(world, x, y, to)) ways.push_back(to);
                if (!ways.empty() && c->held >= static_cast<int>(ways.size()))
                {
                    for (const u_char to : ways) go(to);
                    world.steam_collectors.erase(c);
                }
                return;
            }
            case VENT:
                send_steam(world, x, y, EV_VENT_HOT); // @note "Do not touch this for 3 seconds after steam has been through"
                break;
        }
        /* @note "Steam flows straight when possible, otherwise it turns right (and turns left if it can't do that!)" */
        first_open({ d, static_cast<u_char>((d + 1) & 3), static_cast<u_char>((d + 3) & 3) });
    }

    ::steam_engine &engine_at(::world &world, const ::pos &pos)
    {
        auto engine = std::ranges::find(world.steam_engines, pos, &::steam_engine::pos);
        if (engine == world.steam_engines.end()) engine = world.steam_engines.insert(world.steam_engines.end(), ::steam_engine{ .pos = pos, .cooled = std::time(nullptr) });
        return *engine;
    }

    /* @brief heat wears off while time passes, even while nobody's here */
    void cool(::steam_engine &engine)
    {
        const long long unix_now = std::time(nullptr);
        if (engine.cooled == 0) engine.cooled = unix_now;
        const long long lose = (unix_now - engine.cooled) / COOL_SECONDS;
        if (lose <= 0) return;
        engine.heat = static_cast<int>(std::max<long long>(0, engine.heat - lose));
        engine.cooled += lose * COOL_SECONDS;
    }
}

bool steam_tile_activate(ENetEvent &event, ::world &world, const ::pos &tile)
{
    ::block *block = block_at(world, tile.x_int(), tile.y_int());
    if (block == nullptr) return false;
    if (block->fg != STEAM_STOMPER && block->fg != STEAM_REVOLVER && block->fg != STEAM_LAUNCHER) return false;

    /* @note the game asks again every frame the player stands there: one puff per tile every half second */
    static std::unordered_map<std::string, long long> last{};
    const long long now = now_ms();
    const std::string key = std::format("{}:{}", world.name, cord(tile.x_int(), tile.y_int()));
    if (now - last[key] < 500) return true;
    last[key] = now;
    if (last.size() > 4096) last.clear();

    if (block->fg == STEAM_STOMPER) start_steam(world, tile.x_int(), tile.y_int(), 1); // @note "Jump on this to shoot steam below it!"
    else if (block->fg == STEAM_REVOLVER) start_steam(world, tile.x_int(), tile.y_int(), 3); // @note "Run through this to shoot steam upward!"
    else if (block->state[2] & S_TOGGLE) // @note a charged Steam Launcher: the bounce uses up the charge
    {
        block->state[2] &= ~S_TOGGLE;
        world_tile_update(world, tile);
    }
    return true;
}

bool steam_tile_change(ENetEvent &event, ::world &world, const ::gamePacket &gamePacket)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const ::pos tile{ gamePacket.punch.x_int(), gamePacket.punch.y_int() };
    ::block *block = block_at(world, tile.x_int(), tile.y_int());
    if (block == nullptr) return false;

    if (block->fg == STEAM_ENGINE_ID && gamePacket.id == NUCLEAR_FUEL)
    {
        if (!world.can_edit(*pPeer, tile, false)) throw std::runtime_error("You don't have access to this Steam Engine!");
        if (pPeer->have_slot(::slot(NUCLEAR_FUEL, 0)) <= 0) return true;
        ::steam_engine &engine = engine_at(world, tile);
        cool(engine);
        if (engine.heat >= ENGINE_MAX) throw std::runtime_error("It's as hot as it can get!");
        modify_item_inventory(event, ::slot(NUCLEAR_FUEL, -1));
        const bool was_hot = engine.heat >= ENGINE_HOT;
        engine.heat = std::min(ENGINE_MAX, engine.heat + FUEL_HEAT);
        world_tile_update(world, tile);
        send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, (engine.heat >= ENGINE_HOT) ?
            std::format("The Steam Engine is `4{}`` degrees{}", engine.heat, was_hot ? "." : " - `2it's making steam!``") :
            std::format("The Steam Engine is `w{}`` degrees. It needs 500 to make steam.", engine.heat), 0u, 1u });
        world.save();
        return true;
    }
    if (block->fg == STEAM_ENGINE_ID && gamePacket.id == 32) // @note wrench: how hot is it
    {
        auto engine = std::ranges::find(world.steam_engines, tile, &::steam_engine::pos);
        int heat = 0;
        if (engine != world.steam_engines.end()) { cool(*engine); heat = engine->heat; }
        ::create_dialog dialog = ::create_dialog()
            .set_default_color("`o")
            .add_label_with_icon("big", "`wSteam Engine``", STEAM_ENGINE_ID)
            .add_textbox(std::format("Temperature: `{}{}`` degrees", (heat >= ENGINE_HOT) ? '4' : 'w', heat))
            .add_spacer("small")
            .add_smalltext((heat >= ENGINE_HOT) ?
                "It's hot enough: it makes steam towards the way it faces. It cools down slowly." :
                "Feed it Nuclear Fuel (+100 degrees each). Above 500 degrees it makes steam towards the way it faces.");
        send_varlist(event.peer, { "OnDialogRequest", dialog.end_dialog("steam_engine", "", "OK") });
        return true;
    }
    if (block->fg == CRANK && gamePacket.id == 18) // @note "When punched, this pipe rotates between horizontal and vertical"
    {
        const bool editor = world.can_edit(*pPeer, tile, false);
        if (!editor && !(block->state[2] & S_PUBLIC)) return false; // @note the usual "locked" answer
        block->state[2] ^= S_TOGGLE;
        world_tile_update(world, tile);
        return !editor; // @note the owner's punch still hits it, a visitor only turns it
    }
    return false;
}

void steam_tile_cleared(::world &world, const ::pos &pos)
{
    std::erase_if(world.steam_engines, [&](const ::steam_engine &e) { return e.pos == pos; });
    std::erase_if(world.steam_collectors, [&](const ::steam_collector &c) { return c.pos == pos; });
}

void steam_tick()
{
    static long long last{};
    const long long now = now_ms();
    if (now - last < STEP_MS) return;
    last = now;

    for (::world &world : worlds)
    {
        if (world.visitors == 0)
        {
            world.steam_puffs.clear();
            continue;
        }

        /* @note Steam Engines above 500 degrees puff steam out of their front */
        for (::steam_engine &engine : world.steam_engines)
        {
            if (engine.heat < ENGINE_HOT || now < engine.next) continue;
            const int before = engine.heat;
            cool(engine);
            if (before >= ENGINE_HOT && engine.heat < ENGINE_HOT) world_tile_update(world, engine.pos); // @note cooled off: it stops
            if (engine.heat < ENGINE_HOT) continue;
            engine.next = now + ENGINE_PUFF_MS;
            const ::block &block = world.blocks[cord(engine.pos.x_int(), engine.pos.y_int())];
            start_steam(world, engine.pos.x_int(), engine.pos.y_int(), (block.state[2] & S_LEFT) ? 2 : 0);
        }
        std::erase_if(world.steam_engines, [](const ::steam_engine &e) { return e.heat <= 0; });

        /* @note collectors let go of steam after 20 seconds */
        std::erase_if(world.steam_collectors, [now](const ::steam_collector &c) { return now >= c.until; });

        if (world.steam_puffs.empty()) continue;
        std::vector<::steam_puff> moving{};
        moving.swap(world.steam_puffs);
        std::vector<::steam_puff> next{};
        for (const ::steam_puff &puff : moving) step(world, puff, next, now);
        if (next.size() > MAX_PUFFS) next.resize(MAX_PUFFS);
        for (const ::steam_puff &puff : next) world.steam_puffs.push_back(puff); // @note puffs started during this step stay too
    }
}
