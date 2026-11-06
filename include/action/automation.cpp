#include "pch.hpp"

#include "onVariant/ConsoleMessage.hpp"
#include "onVariant/SetBux.hpp"
#include "tools/create_dialog.hpp"
#include "tools/random.hpp"
#include "tools/sound.hpp"
#include "tools/time.hpp"

#include "automation.hpp"

#include <algorithm>
#include <chrono>

/*
* @note item texts (items.dat): Tesseract Manipulator 6952 "tell it to target any block of your choosing, and it will break them
* AUTOMATICALLY! Blocks broken by this device will not drop gems", Heart of Gaia 6950 "target any tree of your choosing, and it
* will harvest them", Techno-Organic Engine 6954 "breaking blocks OR harvesting trees, and then collecting a target drop".
* tile extras 0x45, 0x46, 0x47 from github.com/voidkle/gtps-guide guide/tile-extras.md.
* blocks are punched one hit at a time like a player (HIT_MS) and need the block's own hit count; trees are harvested every 2 seconds.
* the speed and gem cost are not documented anywhere public, so they are set here.
*/

namespace
{
    constexpr long long INTERVAL_MS = 2000; // @note between harvests
    constexpr long long HIT_MS = 300;       // @note between punches on a block, about a player's punch speed
    constexpr int ENGINE_CAPACITY = 5000;

    long long now_ms()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    bool is_machine(type t) { return t == type::AUTO_BREAK_BLOCK || t == type::AUTO_BREAK_TREE || t == type::AUTO_BREAK; }

    /* @return the gems one break or harvest costs */
    int cost(type machine) { return (machine == type::AUTO_BREAK) ? 2 : 1; }

    /* @brief a plain block the machines may break: no locks, doors, machines or anything else that keeps data */
    bool breakable(short id)
    {
        if (id <= 0) return false;
        const ::item &item = id_to_item(id);
        return item.type == type::FOREGROUND && item.hits > 0 && get_type(item) == '\x00' && !is_tile_lock(id);
    }
    bool is_seed(short id) { return id > 0 && id_to_item(id).type == type::SEED; }

    /* @return the target this machine takes for a picked item, 0 = it can't work on it */
    short target_for(type machine, short picked)
    {
        if (machine == type::AUTO_BREAK_TREE || machine == type::AUTO_BREAK)
        {
            if (is_seed(picked)) return picked;
            if (machine == type::AUTO_BREAK_TREE && is_seed(static_cast<short>(picked + 1))) return static_cast<short>(picked + 1); // @note picking a block means its tree
        }
        if (machine == type::AUTO_BREAK_BLOCK || machine == type::AUTO_BREAK)
            if (breakable(picked)) return picked;
        return 0;
    }

    ::world *world_of(::peer *pPeer)
    {
        if (pPeer->netid == 0 || pPeer->recent_worlds.empty()) return nullptr;
        auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
        return (world != worlds.end()) ? &*world : nullptr;
    }

    void say(ENetPeer *peer, ::peer *pPeer, const std::string &text)
    {
        send_varlist(peer, { "OnTalkBubble", pPeer->netid, text, 0u, 1u });
        on::ConsoleMessage(peer, text);
    }

    ::auto_machine &record(::world &world, const ::pos &pos)
    {
        auto m = std::ranges::find(world.auto_machines, pos, &::auto_machine::pos);
        if (m == world.auto_machines.end()) m = world.auto_machines.emplace(world.auto_machines.end(), ::auto_machine{.pos = pos});
        return *m;
    }

    /* @brief the tile goes away for everyone, with the break sound but no punch animation (netID -1) */
    void clear_tile(::world &world, ::block &block, const ::pos &tile)
    {
        std::erase_if(world.trees, [&](const ::tree &tree) { return tree.pos == tile; });
        block.fg = 0, block.hits[0] = 0;
        block.state[2] &= S_PARENT;
        block.state[3] &= ~S_VANISH;
        ::gamePacket packet{.type = 0x03, .netid = -1, .id = 18, .punch = tile};
        ::blob blob = compress_state(packet);
        peers(world.name, PeerSearch::WORLD, [&blob](ENetPeer *peer) { send_data(*peer, blob); });
    }

    /* @brief a drop the Techno-Organic Engine keeps if it is its target drop, else it falls (or goes to a MAGPLANT) */
    void drop(ENetEvent &event, ::world &world, ::auto_machine &m, type machine, ::slot slot, const ::pos &tile)
    {
        if (slot.count <= 0) return;
        if (machine == type::AUTO_BREAK && slot.id == m.item && m.count + slot.count <= ENGINE_CAPACITY)
        {
            m.count += slot.count;
            ::gamePacket effect{ // @note the item flies into the machine, like a MAGPLANT
                .type  = 0x13,
                .uid   = -1,
                .delay = 200,
                .pos   = ::pos{m.pos.x * 32.0f + 16.0f, m.pos.y * 32.0f + 16.0f},
                .speed = ::pos{tile.x * 32.0f + 16.0f, tile.y * 32.0f + 16.0f},
                .punch = ::pos{static_cast<int>(slot.id), static_cast<int>(slot.id)}
            };
            ::blob blob = compress_state(effect);
            peers(world.name, PeerSearch::WORLD, [&blob](ENetPeer *peer) { send_data(*peer, blob); });
            return;
        }
        add_break_drop(event, slot, tile.by_32(), world);
    }

    enum class result { NOTHING, HIT, DONE };

    /* @brief one punch on the block it is working on (picking the next target block first if needed) */
    result punch_block(ENetEvent &event, ::world &world, ::auto_machine &m, type machine)
    {
        const ::item &target = id_to_item(m.target);
        if (!valid_tile(world, m.working) || world.blocks[cord(m.working.x_int(), m.working.y_int())].fg != m.target)
        {
            m.working = {-1, -1};
            for (int i = 0; i < static_cast<int>(world.blocks.size()); ++i)
                if (world.blocks[i].fg == m.target) { m.working = ::pos{i % 100, i / 100}; break; }
            if (m.working.x < 0) return result::NOTHING;
        }
        const ::pos tile = m.working;
        ::block &block = world.blocks[cord(tile.x_int(), tile.y_int())];
        ++block.hits[0];
        if (block.hits[0] < target.hits)
        {
            ::gamePacket damage{.type = 0x08, .netid = -1, .id = 6, .punch = tile}; // @note TILE_APPLY_DAMAGE: the crack grows, no avatar punches
            ::blob blob = compress_state(damage);
            peers(world.name, PeerSearch::WORLD, [&blob](ENetPeer *peer) { send_data(*peer, blob); });
            return result::HIT;
        }
        clear_tile(world, block, tile);
        m.working = {-1, -1};
        if (!(u_char(target.property) & 04)) // @note same drops as a punch, without the gems
        {
            const bool farmable = target.rarity >= 24;
            if (!RandomRange(0, farmable ? 2 : 4)) drop(event, world, m, machine, ::slot(static_cast<short>(m.target + 1), 1), tile);
            else if (!RandomRange(0, farmable ? 4 : 8)) drop(event, world, m, machine, ::slot(m.target, 1), tile);
        }
        return result::DONE;
    }

    /* @brief harvest one grown tree of the target */
    result harvest(ENetEvent &event, ::world &world, ::auto_machine &m, type machine)
    {
        const ::item &target = id_to_item(m.target);
        for (int i = 0; i < static_cast<int>(world.blocks.size()); ++i)
        {
            ::block &block = world.blocks[i];
            if (block.fg != m.target) continue;
            const ::pos tile{i % 100, i / 100};
            auto t = std::ranges::find(world.trees, tile, &::tree::pos);
            if (t == world.trees.end() || ticks() - t->tick < static_cast<u_int>(target.tick)) continue; // @note not grown yet
            const int fruit = std::max(1, static_cast<int>(t->fruit));
            const short count = static_cast<short>(RandomRange(1, fruit * 3 + 1));
            clear_tile(world, block, tile);
            drop(event, world, m, machine, ::slot(static_cast<short>(m.target - 1), count), tile);
            return result::DONE;
        }
        return result::NOTHING;
    }

    void send(ENetEvent &event, const ::pos &pos, ::create_dialog &dialog)
    {
        dialog.embed_data("tilex", pos.x_int())
              .embed_data("tiley", pos.y_int());
        send_varlist(event.peer, { "OnDialogRequest", dialog.end_dialog("auto_machine", "Close", "Apply") });
    }
}

void auto_machine_wrench(ENetEvent &event, ::world &world, const ::pos &pos)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (!world.can_edit(*pPeer, pos, false)) { play_sfx(event.peer, "audio/cant_place_tile.wav"); return; }
    const short fg = world.blocks[cord(pos.x_int(), pos.y_int())].fg;
    const type machine = static_cast<type>(id_to_item(fg).type);
    if (!is_machine(machine)) return;
    ::auto_machine &m = record(world, pos);

    const std::string what =
        (machine == type::AUTO_BREAK_BLOCK) ? "block" :
        (machine == type::AUTO_BREAK_TREE)  ? "tree" : "block or tree";
    ::create_dialog dialog = ::create_dialog()
        .set_default_color("`o")
        .add_label_with_icon("big", std::format("`w{}``", id_to_item(fg).raw_name), fg)
        .add_spacer("small");
    if (m.target != 0)
        dialog.add_label_with_icon("small", std::format("`oTarget: `w{}``", id_to_item(m.target).raw_name), m.target);
    else
        dialog.add_textbox(std::format("`oNo target yet. Choose the {} it should work on.``", what));
    dialog.add_textbox(std::format("`oGems inside: `w{}`` (each {} costs {} gem{}).``", m.gems,
                        (machine == type::AUTO_BREAK_TREE) ? "harvest" : (machine == type::AUTO_BREAK) ? "break or harvest" : "break",
                        cost(machine), (cost(machine) == 1) ? "" : "s"))
          .add_textbox(m.on ? "`oThe machine is `2ON``. Punch it to turn it off.``" : "`oThe machine is `4OFF``. Punch it to turn it on.``");
    if (machine == type::AUTO_BREAK && m.count > 0)
        dialog.add_label_with_icon("small", std::format("`oHolding `w{} {}``", m.count, id_to_item(m.item).raw_name), m.item)
              .add_button("take_items", "`wRetrieve items``");
    dialog.add_spacer("small")
          .add_item_picker("target", "`wChoose target``", std::format("Choose a {} for the machine", what))
          .add_text_input("add_gems", "Add gems:", static_cast<short>(0), 9);
    if (m.gems > 0) dialog.add_button("take_gems", "`wTake the gems out``");
    send(event, pos, dialog);
}

void auto_machine_edit(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::world *world = world_of(pPeer);
    if (world == nullptr) return;
    const ::pos pos{atoi(hPipe["tilex"].c_str()), atoi(hPipe["tiley"].c_str())};
    if (!valid_tile(*world, pos) || !world->can_edit(*pPeer, pos, false)) return;
    const type machine = static_cast<type>(id_to_item(world->blocks[cord(pos.x_int(), pos.y_int())].fg).type);
    if (!is_machine(machine)) return;
    ::auto_machine &m = record(*world, pos);
    bool changed = false;

    if (hPipe["buttonClicked"] == "take_gems" && m.gems > 0)
    {
        pPeer->gems += m.gems;
        say(event.peer, pPeer, std::format("You took `w{}`` gems out of the machine.", m.gems));
        m.gems = 0, m.on = false;
        on::SetBux(event);
        changed = true;
    }
    else if (hPipe["buttonClicked"] == "take_items" && m.count > 0)
    {
        const short take = static_cast<short>(std::min(m.count, 200));
        if (!pPeer->can_hold(m.item, take)) throw std::runtime_error("You don't have room in your backpack!");
        modify_item_inventory(event, ::slot(m.item, take));
        m.count -= take;
        changed = true;
    }
    if (const short picked = static_cast<short>(atoi(hPipe["target"].c_str())); picked > 0)
    {
        const short target = target_for(machine, picked);
        if (target == 0)
            throw std::runtime_error((machine == type::AUTO_BREAK_TREE) ? "This machine can only harvest trees!" : "This machine can't work on that!");
        if (target != m.target)
        {
            if (machine == type::AUTO_BREAK && m.count > 0) throw std::runtime_error("Retrieve the items inside before you change the target!");
            m.target = target;
            m.item = is_seed(target) ? static_cast<short>(target - 1) : target;
            say(event.peer, pPeer, std::format("The machine now targets `w{}``.", id_to_item(target).raw_name));
            changed = true;
        }
    }
    if (const long long add = atoll(hPipe["add_gems"].c_str()); add > 0)
    {
        if (add > pPeer->gems) throw std::runtime_error("You don't have that many gems!");
        if (static_cast<long long>(m.gems) + add > 1000000000) throw std::runtime_error("That's more gems than it can hold!");
        pPeer->gems -= add;
        m.gems += static_cast<int>(add);
        on::SetBux(event);
        say(event.peer, pPeer, std::format("You put `w{}`` gems into the machine.", add));
        changed = true;
    }
    if (!changed) return;
    world_tile_update(*world, pos);
    pPeer->mysql_update_all();
    world->save();
}

bool auto_machine_punch(ENetEvent &event, ::world &world, const ::pos &pos, bool last_hit)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::auto_machine *m = &record(world, pos);
    if (last_hit && (m->gems > 0 || m->count > 0))
        throw std::runtime_error("Empty the machine before you break it!");
    if (!world.can_edit(*pPeer, pos, false)) return false;

    if (!m->on)
    {
        if (m->target == 0) { say(event.peer, pPeer, "Wrench the machine and choose a target first!"); return false; }
        const type machine = static_cast<type>(id_to_item(world.blocks[cord(pos.x_int(), pos.y_int())].fg).type);
        if (m->gems < cost(machine)) { say(event.peer, pPeer, "The machine needs gems to run! Wrench it to add some."); return false; }
    }
    m->on = !m->on;
    m->next = now_ms() + HIT_MS;
    say(event.peer, pPeer, m->on ? "The machine is now `2ON``." : "The machine is now `4OFF``.");
    world_tile_update(world, pos);
    return false;
}

void automation_tick()
{
    static long long last = 0;
    const long long now = now_ms();
    if (now - last < 100) return;
    last = now;

    for (::world &world : worlds)
    {
        if (world.auto_machines.empty()) continue;
        ENetPeer *someone = nullptr; // @note machines run while someone is in the world, like the rest of a loaded world
        peers(world.name, PeerSearch::WORLD, [&someone](ENetPeer *peer) { if (someone == nullptr) someone = peer; });
        if (someone == nullptr) continue;
        ENetEvent event{};
        event.peer = someone;

        for (std::size_t i = 0; i < world.auto_machines.size(); ++i)
        {
            ::auto_machine &m = world.auto_machines[i];
            if (!m.on || now < m.next || !valid_tile(world, m.pos)) continue;
            const type machine = static_cast<type>(id_to_item(world.blocks[cord(m.pos.x_int(), m.pos.y_int())].fg).type);
            if (!is_machine(machine)) { m.on = false; continue; }
            if (m.gems < cost(machine) || m.target == 0 || (machine == type::AUTO_BREAK && m.count >= ENGINE_CAPACITY))
            {
                m.on = false; // @note out of gems, or full: it stops by itself
                world_tile_update(world, m.pos);
                continue;
            }
            const bool tree = is_seed(m.target);
            m.next = now + (tree ? INTERVAL_MS : HIT_MS);
            if ((tree ? harvest(event, world, m, machine) : punch_block(event, world, m, machine)) == result::DONE)
            {
                m.gems -= cost(machine);
                if (m.gems < cost(machine)) m.on = false;
                world_tile_update(world, m.pos);
            }
        }
    }
}
