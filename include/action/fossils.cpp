#include "pch.hpp"
#include "action/carnival.hpp"
#include "action/growpass.hpp"

#include "onVariant/ConsoleMessage.hpp"
#include "tools/sound.hpp"
#include "tools/random.hpp"
#include "radiation.hpp" // @note give_or_drop()

#include <algorithm>
#include <chrono>

#include "fossils.hpp"

namespace
{
    constexpr long long SPAWN_EVERY_MS = 10ll * 60 * 1000; // @note real Growtopia checks every 10 minutes

    /* @note the rock and the Fossil Rock it can become */
    constexpr std::pair<short, short> rock_to_fossil[] = {
        {10, 3918},   // Rock -> Fossil Rock
        {944, 3920},  // Boulder -> Fossil Boulder
        {1538, 3922}, // Deep Rock -> Deep Fossil Rock
        {2492, 3924}, // Asteroid -> Fossilroid
        {1134, 3926}, // Mars Rock -> Alien Fossil Rock
        {3930, 3928}, // Igneous Rock -> Igneous Fossil Rock
    };

    short fossil_of(short rock)
    {
        for (const auto &[r, f] : rock_to_fossil) if (r == rock) return f;
        return 0;
    }
    short rock_of(short fossil)
    {
        for (const auto &[r, f] : rock_to_fossil) if (f == fossil) return r;
        return 10;
    }

    long long now_ms()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    /* @note what a Fossil Prep Station reveals. {item, weight}. a Polished Fossil uses the second weight (better odds on the rare ones) */
    struct prep_prize { short item; int normal; int polished; };
    constexpr prep_prize prep_prizes[] = {
        {3940, 24, 14}, // Ammonite Block
        {3948, 16, 10}, // Amber Block
        {3942, 12, 10}, // Dino Ribcage
        {4094, 6, 4},   // Prehistoric Flowering Fern
        {4096, 6, 4},   // Prehistoric Red Fern
        {4098, 6, 4},   // Prehistoric Fern
        {3946, 4, 5},   // Ancient Plant Seed
        {4126, 3, 4},   // Stone Spear
        {3944, 4, 6},   // Dinosaur Mask
        {3950, 4, 7},   // T-Rex Skull
        {6838, 3, 5},   // Amber Resin
        {4090, 2, 4},   // Plant DNA Strand X-277
        {4092, 2, 4},   // Plant DNA Strand Y-985
        {4082, 2, 4},   // Dino DNA Strand A-273
        {4084, 2, 4},   // Dino DNA Strand C-904
        {4086, 2, 4},   // Dino DNA Strand G-137
        {4088, 2, 4},   // Dino DNA Strand T-658
        {4120, 1, 3},   // Raptor DNA Strand Alpha
        {4122, 1, 3},   // Raptor DNA Strand Omega
    };

    short roll_prep(bool polished)
    {
        int total = 0;
        for (const prep_prize &p : prep_prizes) total += polished ? p.polished : p.normal;
        int roll = RandomRange(0, total - 1);
        for (const prep_prize &p : prep_prizes)
        {
            const int w = polished ? p.polished : p.normal;
            if (roll < w) return p.item;
            roll -= w;
        }
        return prep_prizes[0].item;
    }

    int &life_of(::world &world, const ::pos &pos)
    {
        auto it = std::ranges::find(world.fossil_life, pos, &std::pair<::pos, int>::first);
        if (it == world.fossil_life.end()) it = world.fossil_life.insert(world.fossil_life.end(), std::make_pair(pos, static_cast<int>(RandomRange(3, 8))));
        return it->second;
    }

    void bubble(ENetEvent &event, const std::string &text)
    {
        const ::peer *pPeer = static_cast<::peer*>(event.peer->data);
        send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, text, 0u, 1u });
    }

    bool hammer_tap(ENetEvent &event, ::world &world, const ::pos &pos, ::block &block)
    {
        if (block.state[2] & S_TOGGLE) // @note already loose: hitting it again breaks the fossil
        {
            block.fg = rock_of(block.fg);
            block.state[2] &= ~S_TOGGLE;
            block.hits[0] = 0;
            std::erase_if(world.fossil_life, [&](const auto &p) { return p.first == pos; });
            world_tile_update(world, pos);
            bubble(event, "`4I smashed a Fossil!``");
            play_sfx(event.peer, "audio/rock_destroy.wav");
            world.save();
            return true;
        }
        int &life = life_of(world, pos);
        if (--life > 0)
        {
            play_sfx(event.peer, "audio/rock_hit.wav");
            return true;
        }
        std::erase_if(world.fossil_life, [&](const auto &p) { return p.first == pos; });
        block.state[2] |= S_TOGGLE;
        world_tile_update(world, pos);
        bubble(event, "`2I unearthed a Fossil!`` I better be careful getting it out...");
        play_sfx(event.peer, "audio/secret.wav");
        world.save();
        return true;
    }

    bool brush(ENetEvent &event, ::world &world, const ::pos &pos)
    {
        ::peer *pPeer = static_cast<::peer*>(event.peer->data);
        auto fossil = std::ranges::find_if(world.objects, [&](const ::object &o) { return o.id == FOSSIL_ITEM && o.pos.by_32(true) == pos; });
        if (fossil == world.objects.end()) return false; // @note no fossil here, let the brush do nothing special

        if (std::ranges::find(world.fresh_fossils, fossil->uid) == world.fresh_fossils.end() || fossil->count != 1)
        {
            bubble(event, "You can only brush Fossils that have never been picked up!");
            return true;
        }
        const ::pos where = fossil->pos;
        const u_int uid = fossil->uid;
        world.objects.erase(fossil);
        std::erase(world.fresh_fossils, uid);
        remove_object(event, static_cast<int>(uid));
        add_object(event, ::slot(POLISHED_FOSSIL, 1), where, world);
        modify_item_inventory(event, ::slot(FOSSIL_BRUSH, -1));
        bubble(event, "You polished the `2Fossil``, using up 1 `2Fossil Brush``.");
        (void)pPeer;
        return true;
    }

    bool prep_station(ENetEvent &event, ::world &world, const ::gamePacket &gamePacket, ::block &block)
    {
        ::peer *pPeer = static_cast<::peer*>(event.peer->data);
        auto machine = std::ranges::find(world.timed_machines, gamePacket.punch, &::timed_machine::pos);
        const bool loaded = machine != world.timed_machines.end() && machine->item != 0;
        const long long now = std::time(nullptr);

        if (gamePacket.id == 18)
        {
            if (!loaded) return false; // @note empty: punching breaks it as usual
            if (now < machine->ready)
            {
                const long long left = machine->ready - now;
                bubble(event, std::format("It's still dissolving the rock. `w{}h {}m`` left.", left / 3600, (left % 3600 + 59) / 60));
                return true;
            }
            const short prize = roll_prep(machine->item == POLISHED_FOSSIL);
            world.timed_machines.erase(machine);
            block.state[2] &= ~S_TOGGLE;
            world_tile_update(world, gamePacket.punch);
            give_or_drop(event, world, prize, 1);
            const std::string news = std::format("`o{}`` `oprepared a fossil and found `2{}``!", pPeer->display_growid, id_to_item(prize).raw_name);
            peers(world.name, PeerSearch::WORLD, [&news](ENetPeer *p) { on::ConsoleMessage(p, news); });
            bubble(event, std::format("It was a `2{}``!", id_to_item(prize).raw_name));
            play_sfx(event.peer, "audio/secret.wav");
            grow_pass_points(event.peer, 15);
            ring_progress(event.peer, RT_FOSSILS, 1);
            world.save();
            return true;
        }
        if (gamePacket.id != FOSSIL_ITEM && gamePacket.id != POLISHED_FOSSIL) return false;
        if (!world.can_edit(*pPeer, gamePacket.punch, false)) throw std::runtime_error("That's not your Fossil Prep Station!");
        if (loaded) throw std::runtime_error("There's already a fossil in there!");

        /* @note "anywhere from an hour to a day depending on how much needs dissolving". a polished one has less rock left */
        const long long hours = (gamePacket.id == POLISHED_FOSSIL) ? RandomRange(1, 12) : RandomRange(1, 24);
        modify_item_inventory(event, ::slot(gamePacket.id, -1));
        ::timed_machine m{ .pos = gamePacket.punch, .item = static_cast<short>(gamePacket.id), .start = now, .ready = now + hours * 3600 };
        if (machine != world.timed_machines.end()) *machine = m;
        else world.timed_machines.push_back(m);
        block.state[2] |= S_TOGGLE;
        world_tile_update(world, gamePacket.punch);
        bubble(event, std::format("You put the `2{}`` in the tank. It'll take about `w{} hour{}`` to dissolve the rock.", id_to_item(gamePacket.id).raw_name, hours, hours == 1 ? "" : "s"));
        world.save();
        return true;
    }
}

bool fossil_tile_change(ENetEvent &event, ::world &world, const ::gamePacket &gamePacket)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::block &block = world.blocks[cord(gamePacket.punch.x_int(), gamePacket.punch.y_int())];
    const auto kind = id_to_item(block.fg).type;

    if (kind == type::FOSSIL_PREP_STATION) return prep_station(event, world, gamePacket, block);
    if (gamePacket.id == FOSSIL_BRUSH) return brush(event, world, gamePacket.punch);

    if (kind == type::FOSSIL && gamePacket.id == 18 && pPeer->clothing[clothing::HAND] == ROCK_HAMMER)
    {
        if (!world.can_edit(*pPeer, gamePacket.punch, true))
        {
            play_sfx(event.peer, "audio/punch_locked.wav");
            return true;
        }
        return hammer_tap(event, world, gamePacket.punch, block);
    }
    return false;
}

bool fossil_broken(ENetEvent &event, ::world &world, const ::pos &pos, bool open)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    std::erase_if(world.fossil_life, [&](const auto &p) { return p.first == pos; });
    if (open && pPeer->clothing[clothing::HAND] == ROCK_CHISEL)
    {
        const int uid = add_object(event, ::slot(FOSSIL_ITEM, 1), pos.by_32(), world);
        world.fresh_fossils.push_back(static_cast<u_int>(uid));
        send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, "`2I got the Fossil out!``", 0u, 1u });
    }
    else if (open) send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, "`4I destroyed the Fossil!`` I should have used a `2Rock Chisel``...", 0u, 1u });
    return true; // @note a Fossil Rock never drops gems, seeds or itself
}

bool fossil_spawn(::world &world, ::pos *where)
{
    std::vector<int> rocks{};
    for (int i = 0; i < static_cast<int>(world.blocks.size()); ++i)
        if (fossil_of(world.blocks[i].fg) != 0) rocks.push_back(i);
    if (rocks.empty()) return false;

    const int i = rocks[RandomRange(0, static_cast<int>(rocks.size()) - 1)];
    ::block &block = world.blocks[i];
    block.fg = fossil_of(block.fg);
    block.hits[0] = 0;
    block.state[2] &= ~S_TOGGLE;
    const ::pos tile{ static_cast<float>(i % 100), static_cast<float>(i / 100) };
    if (where != nullptr) *where = tile;
    world_tile_update(world, tile);
    print(std::format("[fossil] a {} appeared in {} at {},{}", id_to_item(block.fg).raw_name, world.name, i % 100, i / 100));
    world.save();
    return true;
}

void fossil_tick()
{
    static long long last = now_ms();
    const long long now = now_ms();
    if (now - last < SPAWN_EVERY_MS) return;
    last = now;

    for (::world &world : worlds)
    {
        if (world.visitors <= 0) continue;
        /* @note the busier the world, the likelier: 5% a check for each person in it, up to 50% */
        const int chance = std::min(50, 5 * static_cast<int>(world.visitors));
        if (RandomRange(1, 100) <= chance) fossil_spawn(world);
    }
}

void fossil_command(ENetEvent &event, const std::string_view)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
    if (pPeer->netid == 0 || world == worlds.end()) return on::ConsoleMessage(event.peer, "Enter a world first.");
    ::pos where{};
    on::ConsoleMessage(event.peer, fossil_spawn(*world, &where) ?
        std::format("`2A Fossil Rock appeared at {}, {}!``", where.x_int(), where.y_int()) :
        "There's no Rock here for a fossil to appear in (Rock, Boulder, Deep Rock, Asteroid, Mars Rock or Igneous Rock).");
}

void ready_command(ENetEvent &event, const std::string_view)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
    if (pPeer->netid == 0 || world == worlds.end()) return on::ConsoleMessage(event.peer, "Enter a world first.");
    const long long now = std::time(nullptr);
    int done = 0;
    for (::timed_machine &m : world->timed_machines)
    {
        if (m.item == 0) continue;
        const long long length = m.ready - m.start;
        m.ready = now, m.start = now - length;
        world_tile_update(*world, m.pos);
        ++done;
    }
    on::ConsoleMessage(event.peer, std::format("`2{}`` Fossil Prep Station{} and Geiger Charger{} finished.", done, done == 1 ? "" : "s", done == 1 ? "" : "s"));
}
