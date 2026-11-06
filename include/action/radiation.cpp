#include "pch.hpp"
#include "action/growpass.hpp"

#include "onVariant/ConsoleMessage.hpp"
#include "onVariant/SetClothing.hpp"
#include "tools/sound.hpp"
#include "tools/random.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "radiation.hpp"

namespace
{
    constexpr long long IRRADIATED_SECONDS = 30 * 60;
    constexpr int HAND_CHARGE_SECONDS      = 30 * 60; // @note a Dead Geiger Counter held in hand
    constexpr long long CHARGER_SECONDS    = 60 * 60; // @note "half the speed of hand-charging"
    constexpr long long STILL_MS           = 1500;    // @note how long to stand on the source to find it
    constexpr long long CHAMELEON_MS       = 3000;    // @note how long to stand still before blending in
    constexpr float GEIGER_PARTICLE        = 114.0f;

    long long now_ms()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    /* @note what the Geiger Counter finds, with the chance (out of 100) of each. from Nomikai's GeigerNormalDropsWithoutLuminous */
    struct geiger_prize { short item; float chance; short count; };
    constexpr geiger_prize geiger_prizes[] = {
        {6416, 7.65f, 5},  // Star Fuel
        {3196, 7.29f, 1},  // Nuclear Fuel
        {2206, 22.43f, 1}, // Radioactive Chemical
        {1500, 7.18f, 3},  // Orange Stuff
        {1498, 7.74f, 3},  // Purple Stuff
        {2804, 6.83f, 3},  // Green Stuff
        {2806, 7.38f, 3},  // Blue Stuff
        {8270, 7.33f, 3},  // Red Stuff
        {8272, 8.17f, 3},  // White Stuff
        {8274, 7.02f, 3},  // Black Stuff
        {2244, 3.36f, 1},  // Green Crystal
        {2246, 3.20f, 1},  // Blue Crystal
        {2242, 2.82f, 1},  // Red Crystal
        {2248, 1.25f, 1},  // White Crystal
        {2250, 0.14f, 1},  // Black Crystal
        {3792, 0.05f, 1},  // Glowstick
        {3306, 0.02f, 1},  // D Battery
        {4654, 0.11f, 1},  // Geiger Charger
    };

    const geiger_prize &roll_prize()
    {
        float total = 0.0f;
        for (const geiger_prize &p : geiger_prizes) total += p.chance;
        float roll = static_cast<float>(RandomRange(0, 1'000'000)) / 1'000'000.0f * total;
        for (const geiger_prize &p : geiger_prizes)
        {
            if (roll < p.chance) return p;
            roll -= p.chance;
        }
        return geiger_prizes[2];
    }

    ::world *world_of(const ::peer &peer)
    {
        if (peer.netid == 0) return nullptr;
        auto it = std::ranges::find(worlds, peer.recent_worlds.back(), &::world::name);
        return (it != worlds.end()) ? &*it : nullptr;
    }

    void new_source(::world &world)
    {
        const int height = static_cast<int>(world.blocks.size() / 100);
        world.geiger = ::pos{
            static_cast<float>(RandomRange(2, 100 - 3)),
            static_cast<float>(RandomRange(2, std::max(3, height - 9)))
        };
    }

    ::pos tile_of(const ::peer &peer) { return ::pos{ std::floor((peer.pos.x + 10.0f) / 32.0f), std::floor((peer.pos.y + 15.0f) / 32.0f) }; }

    /* @brief swap what's in the player's hand for another item (Geiger Counter <-> Dead Geiger Counter) */
    void swap_hand(ENetPeer *peer, short from, short to)
    {
        ::peer *pPeer = static_cast<::peer*>(peer->data);
        ENetEvent event{ .peer = peer };
        modify_item_inventory(event, ::slot(from, -1));
        modify_item_inventory(event, ::slot(to, 1));
        pPeer->clothing[clothing::HAND] = to;
        pPeer->update_effects();
        send_varlist(peer, { "OnEquipNewItem", static_cast<int>(to) }, pPeer->netid);
        on::SetClothing(peer);
    }

    void ping(ENetPeer *peer, float variable)
    {
        const ::peer *pPeer = static_cast<::peer*>(peer->data);
        ::gamePacket packet{
            .type = 0x11, // @note PACKET_SEND_PARTICLE_EFFECT, only the holder sees it
            .pos = ::pos{ pPeer->pos.x + 12.0f, pPeer->pos.y + 15.0f },
            .speed = ::pos{ variable, GEIGER_PARTICLE }
        };
        send_data(*peer, compress_state(packet));
    }

    void disguise(ENetPeer *peer, ::world &world, int id)
    {
        ::peer *pPeer = static_cast<::peer*>(peer->data);
        pPeer->disguised = (id != 0);
        peers(world.name, PeerSearch::WORLD, [&](ENetPeer *p) { send_varlist(p, { "OnDisguiseChanged", id }, pPeer->netid); });
    }

    void found_something(ENetPeer *peer, ::world &world)
    {
        ::peer *pPeer = static_cast<::peer*>(peer->data);
        const geiger_prize &prize = roll_prize();
        const ::item &item = id_to_item(prize.item);

        ENetEvent event{ .peer = peer };
        give_or_drop(event, world, prize.item, prize.count);
        send_varlist(peer, { "OnTalkBubble", pPeer->netid, std::format("`wI found `2{} {}``!", prize.count, item.raw_name), 0u, 0u });
        const std::string news = std::format("`o{}`` `ofound `2{} {}``!", pPeer->display_growid, prize.count, item.raw_name);
        peers(world.name, PeerSearch::WORLD, [&news](ENetPeer *p) { on::ConsoleMessage(p, news); });
        ping(peer, 3.0f);
        play_sfx(peer, "audio/secret.wav");

        new_source(world); // @note the next find is somewhere else
        swap_hand(peer, GEIGER_COUNTER, DEAD_GEIGER_COUNTER);
        pPeer->geiger_charge = 0;
        pPeer->irradiated_until = std::time(nullptr) + IRRADIATED_SECONDS;
        on::ConsoleMessage(peer, "`oYou are aglow with radiation! (`$Irradiated`` mod added, `$30 mins`` left)``");
        on::SetClothing(peer); // @note the green glow
        pPeer->mysql_update_all();
        grow_pass_points(peer, 15);
    }

    void geiger_tick(ENetPeer *peer, ::world &world, long long now)
    {
        ::peer *pPeer = static_cast<::peer*>(peer->data);
        if (irradiated(*pPeer)) return; // @note an Irradiated player's counter stays silent

        if (!valid_tile(world, world.geiger)) new_source(world);
        const ::pos here = tile_of(*pPeer);
        const float dx = here.x - world.geiger.x, dy = here.y - world.geiger.y;
        const int distance = static_cast<int>(std::lround(std::sqrt(dx * dx + dy * dy)));

        if (distance == 0)
        {
            if (pPeer->geiger_found_since == 0) pPeer->geiger_found_since = now;
            if (now - pPeer->still_since >= STILL_MS && now - pPeer->geiger_found_since >= STILL_MS)
            {
                pPeer->geiger_found_since = 0;
                found_something(peer, world);
                return;
            }
        }
        else pPeer->geiger_found_since = 0;

        /* @note red far away, yellow closer, green closest. the pings speed up as you get close */
        long long delay{};
        float color{};
        if (distance <= 0)       delay = 800,  color = 2.0f;
        else if (distance < 4)   delay = std::max(800, distance * 450), color = 2.0f;
        else if (distance < 8)   delay = 1500, color = 2.0f;
        else if (distance < 15)  delay = 1500, color = 1.0f;
        else if (distance <= 30) delay = 1500, color = 0.0f;
        else return; // @note too far away to pick anything up
        if (now - pPeer->geiger_pulse < delay) return;
        pPeer->geiger_pulse = now;
        ping(peer, color);
    }

    void charge_tick(ENetPeer *peer, long long now)
    {
        ::peer *pPeer = static_cast<::peer*>(peer->data);
        if (now - pPeer->geiger_charge_tick < 1000) return;
        pPeer->geiger_charge_tick = now;
        if (++pPeer->geiger_charge < HAND_CHARGE_SECONDS) return;

        pPeer->geiger_charge = 0;
        swap_hand(peer, DEAD_GEIGER_COUNTER, GEIGER_COUNTER);
        on::ConsoleMessage(peer, "`oYour `2Geiger Counter`` is fully charged! (`$Charging Geiger Counter`` mod removed)``");
        play_sfx(peer, "audio/pay_time.wav");
        pPeer->mysql_update_all();
    }
}

bool irradiated(const ::peer &peer)
{
    return peer.irradiated_until > std::time(nullptr);
}

std::string radiation_effects(const ::peer &peer)
{
    std::string lines{};
    if (irradiated(peer))
    {
        const long long left = peer.irradiated_until - std::time(nullptr);
        lines += std::format("add_label_with_icon|small|`wIrradiated`` `o({} mins left)``|left|2206|\n", (left + 59) / 60);
    }
    if (peer.clothing[clothing::HAND] == DEAD_GEIGER_COUNTER)
        lines += std::format("add_label_with_icon|small|`wCharging Geiger Counter`` `o({}%)``|left|{}|\n", peer.geiger_charge * 100 / HAND_CHARGE_SECONDS, DEAD_GEIGER_COUNTER);
    return lines;
}

void give_or_drop(ENetEvent &event, ::world &world, short id, short count)
{
    const ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const u_short left = modify_item_inventory(event, ::slot(id, count));
    if (left > 0) add_drop(event, ::slot(id, static_cast<short>(left)), pPeer->pos, world);
}

bool radiation_tile_change(ENetEvent &event, ::world &world, const ::gamePacket &gamePacket)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::block &block = world.blocks[cord(gamePacket.punch.x_int(), gamePacket.punch.y_int())];
    if (id_to_item(block.fg).type != type::GEIGER_CHARGER) return false;

    auto machine = std::ranges::find(world.timed_machines, gamePacket.punch, &::timed_machine::pos);
    const bool loaded = machine != world.timed_machines.end() && machine->item != 0;

    if (gamePacket.id == 18) // @note punch: collect when it's done, otherwise it can be broken (only when empty)
    {
        if (!loaded) return false;
        const long long now = std::time(nullptr);
        if (now < machine->ready)
        {
            const long long left = machine->ready - now;
            send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, std::format("It's still charging! `w{}m`` left.", (left + 59) / 60), 0u, 1u });
            return true;
        }
        if (!pPeer->can_hold(GEIGER_COUNTER, 1)) throw std::runtime_error("You don't have room for the Geiger Counter!");
        modify_item_inventory(event, ::slot(GEIGER_COUNTER, 1));
        world.timed_machines.erase(machine);
        block.state[2] &= ~S_TOGGLE;
        world_tile_update(world, gamePacket.punch);
        send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, "You took out a fully charged `2Geiger Counter``!", 0u, 1u });
        play_sfx(event.peer, "audio/pay_time.wav");
        world.save();
        return true;
    }
    if (gamePacket.id == DEAD_GEIGER_COUNTER || gamePacket.id == GEIGER_COUNTER)
    {
        if (gamePacket.id == GEIGER_COUNTER) throw std::runtime_error("That Geiger Counter is already charged!");
        if (!world.can_edit(*pPeer, gamePacket.punch, false)) throw std::runtime_error("That's not your Geiger Charger!");
        if (loaded) throw std::runtime_error("There's already a Geiger Counter charging in there!");

        modify_item_inventory(event, ::slot(DEAD_GEIGER_COUNTER, -1));
        if (pPeer->clothing[clothing::HAND] == DEAD_GEIGER_COUNTER && pPeer->have_slot(::slot(DEAD_GEIGER_COUNTER, 0)) == 0)
        {
            pPeer->clothing[clothing::HAND] = 0;
            on::SetClothing(event.peer);
        }
        const long long now = std::time(nullptr);
        ::timed_machine m{ .pos = gamePacket.punch, .item = DEAD_GEIGER_COUNTER, .start = now, .ready = now + CHARGER_SECONDS };
        if (machine != world.timed_machines.end()) *machine = m;
        else world.timed_machines.push_back(m);
        block.state[2] |= S_TOGGLE;
        world_tile_update(world, gamePacket.punch);
        send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, "You put a `2Dead Geiger Counter`` in to charge. Come back in `w1 hour``!", 0u, 1u });
        world.save();
        return true;
    }
    return false;
}

void radiation_moved(ENetEvent &event)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (std::abs(pPeer->pos.x - pPeer->last_pos.x) < 1.0f && std::abs(pPeer->pos.y - pPeer->last_pos.y) < 1.0f) return;
    pPeer->last_pos = pPeer->pos;
    pPeer->still_since = now_ms();
    pPeer->geiger_found_since = 0;
    if (pPeer->disguised)
        if (::world *world = world_of(*pPeer); world != nullptr) disguise(event.peer, *world, 0);
}

void radiation_tick()
{
    static long long last{};
    const long long now = now_ms();
    if (now - last < 200) return;
    last = now;

    peers("", PeerSearch::EVERYONE, [now](ENetPeer *peer)
    {
        ::peer *pPeer = static_cast<::peer*>(peer->data);
        if (pPeer == nullptr || !pPeer->authenticated) return;

        if (pPeer->irradiated_until != 0 && !irradiated(*pPeer))
        {
            pPeer->irradiated_until = 0;
            on::ConsoleMessage(peer, "`oYou have recovered. (`$Irradiated`` mod removed)``");
            if (pPeer->netid != 0) on::SetClothing(peer);
        }

        ::world *world = world_of(*pPeer);
        if (world == nullptr) return;
        if (pPeer->still_since == 0) pPeer->still_since = now;

        const short hand = static_cast<short>(pPeer->clothing[clothing::HAND]);
        if (hand == GEIGER_COUNTER && pPeer->have_slot(::slot(GEIGER_COUNTER, 0)) > 0) geiger_tick(peer, *world, now);
        else if (hand == DEAD_GEIGER_COUNTER && pPeer->have_slot(::slot(DEAD_GEIGER_COUNTER, 0)) > 0) charge_tick(peer, now);

        if (hand != GREAT_CHAMELEON)
        {
            if (pPeer->disguised) disguise(peer, *world, 0);
        }
        else if (!pPeer->disguised && now - pPeer->still_since >= CHAMELEON_MS)
        {
            /* @note blend into what's behind the player: the foreground they stand in, else the background */
            const ::pos here = tile_of(*pPeer);
            if (valid_tile(*world, here))
            {
                const ::block &block = world->blocks[cord(here.x_int(), here.y_int())];
                const int id = (block.fg != 0) ? block.fg : block.bg;
                if (id != 0) disguise(peer, *world, id);
            }
        }
    });
}

void geiger_command(ENetEvent &event, const std::string_view)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::world *world = world_of(*pPeer);
    if (world == nullptr) return on::ConsoleMessage(event.peer, "Enter a world first.");
    if (!valid_tile(*world, world->geiger)) new_source(*world);
    on::ConsoleMessage(event.peer, std::format("`2The radiation source in this world is at {}, {}.``", world->geiger.x_int(), world->geiger.y_int()));
}
