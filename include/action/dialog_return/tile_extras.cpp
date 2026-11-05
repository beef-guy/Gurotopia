#include "pch.hpp"

#include "onVariant/ConsoleMessage.hpp"
#include "tools/create_dialog.hpp"

#include "tile_extras.hpp"

#include <algorithm>

namespace
{
    constexpr short WORLD_LOCK = 242;
    constexpr short DIAMOND_LOCK = 1796;

    ::world *peer_world(::peer *pPeer)
    {
        if (pPeer->netid == 0) return nullptr;
        auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
        return (world != worlds.end()) ? &*world : nullptr;
    }

    /* @return the tile from tilex|tiley, if it holds a block of this type */
    bool read_tile(const ::hPipe &hPipe, const ::world &world, u_char block_type, ::pos &pos)
    {
        pos = ::pos{atoi(hPipe["tilex"].c_str()), atoi(hPipe["tiley"].c_str())};
        if (!valid_tile(world, pos)) return false;
        return id_to_item(world.blocks[cord(pos.x_int(), pos.y_int())].fg).type == block_type;
    }

    /* @note locks are money, they can't go in a machine */
    bool sellable(short id)
    {
        const ::item &item = id_to_item(id);
        return item.id != 0 && !(item.cat & CAT_UNTRADEABLE) && item.type != type::LOCK && item.type != type::FIST && item.type != type::WRENCH;
    }

    int storage_capacity(short id)
    {
        switch (id)
        {
            case 6286: return 20; // @note Storage Box Xtreme - Level 1. source: https://growtopiawiki.com/w/Storage_Box_Xtreme_-_Level_1
            case 6288: return 40; // @note Level 2
            case 6290: return 90; // @note Level 3
        }
        return 20;
    }

    void update_tile(ENetEvent &event, ::world &world, const ::pos &pos)
    {
        ::block &block = world.blocks[cord(pos.x_int(), pos.y_int())];
        send_tile_update(event, ::gamePacket{.id = block.fg, .punch = pos}, block, world);
    }

    std::string locks_text(int count)
    {
        return std::format("{} World Lock{}", count, (count == 1) ? "" : "s");
    }

    /* @brief give World Locks, as Diamond Locks where 100 fit together. @return how many could not fit */
    int give_locks(ENetEvent &event, ::peer *pPeer, int count)
    {
        while (count >= 100 && pPeer->can_hold(DIAMOND_LOCK, 1))
        {
            modify_item_inventory(event, ::slot(DIAMOND_LOCK, 1));
            count -= 100;
        }
        const int room = pPeer->can_hold(WORLD_LOCK, 1) ? 200 - pPeer->have_slot(::slot(WORLD_LOCK, 0)) : 0;
        const int given = std::min(count, room);
        if (given > 0) modify_item_inventory(event, ::slot(WORLD_LOCK, static_cast<short>(given)));
        return count - given;
    }
}

/* ---------------- Vending Machine ---------------- */

void vending_wrench(ENetEvent &event, ::world &world, const ::pos &pos)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const ::item &machine = id_to_item(world.blocks[cord(pos.x_int(), pos.y_int())].fg);

    auto vend = std::ranges::find(world.vendings, pos, &::vending::pos);
    const bool stocked = vend != world.vendings.end() && vend->count > 0;

    ::create_dialog dialog = ::create_dialog()
        .set_default_color("`o")
        .add_label_with_icon("big", std::format("`w{}``", machine.raw_name), machine.id)
        .add_spacer("small")
        .embed_data("tilex", pos.x_int())
        .embed_data("tiley", pos.y_int());

    if (world.can_edit(*pPeer, pos, false)) // @note the machine's owner
    {
        if (!stocked)
        {
            dialog.add_textbox("This machine is empty.")
                  .add_item_picker("stockitem", "`wPut an item in``", "Choose an item to put in the machine!");
        }
        else
        {
            const ::item &item = id_to_item(vend->id);
            dialog.add_label_with_icon("small", std::format("`wThe machine contains a total of {} `2{}``.``", vend->count, item.raw_name), item.id)
                  .add_button("pullstock", "`wRemove items``");
            if (pPeer->have_slot(::slot(vend->id, 0)) > 0) dialog.add_button("addstock", std::format("`wAdd {} from backpack``", item.raw_name));
            dialog.add_spacer("small")
                  .add_text_input("setprice", "Price:", static_cast<short>(std::abs(vend->price)), 5)
                  .add_checkbox("chk_peritem", "World Locks per Item", vend->price >= 0)
                  .add_checkbox("chk_perlock", "Items per World Lock", vend->price < 0);
            if (vend->price == 0) dialog.add_smalltext("`4Set a price, or nobody can buy from this machine.``");
        }
        if (vend != world.vendings.end() && vend->earned > 0)
        {
            dialog.add_spacer("small")
                  .add_textbox(std::format("The machine has collected `5{}``.", locks_text(vend->earned)))
                  .add_button("withdraw", "`wCollect World Locks``");
        }
        send_varlist(event.peer, { "OnDialogRequest", dialog.add_spacer("small").end_dialog("vending", "Close", stocked ? "Update" : "") });
        return;
    }

    if (!stocked || vend->price == 0)
    {
        send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, "This machine is out of order.", 0u, 1u });
        return;
    }
    const ::item &item = id_to_item(vend->id);
    dialog.add_label_with_icon("small", std::format("`w{}``", item.raw_name), item.id)
          .add_textbox(std::format("The machine contains `w{}``.", vend->count))
          .add_textbox((vend->price > 0) ?
              std::format("Each one costs `5{}``.", locks_text(vend->price)) :
              std::format("You can get `w{}`` for `51 World Lock``.", -vend->price))
          .add_text_input("buycount", (vend->price > 0) ? "How many would you like?" : "How many World Locks will you spend?", 1, 5)
          .embed_data("expectprice", vend->price)
          .embed_data("expectitem", vend->id)
          .add_spacer("small");
    send_varlist(event.peer, { "OnDialogRequest", dialog.end_dialog("vending_buy", "Cancel", "Buy") });
}

void vending_edit(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::world *world = peer_world(pPeer);
    ::pos pos{};
    if (world == nullptr || !read_tile(hPipe, *world, type::VENDING_MACHINE, pos)) return;
    if (!world->can_edit(*pPeer, pos, false)) return;

    auto vend = std::ranges::find(world->vendings, pos, &::vending::pos);
    const std::string button = hPipe["buttonClicked"];

    if (const short id = static_cast<short>(atoi(hPipe["stockitem"].c_str())); id > 0 && (vend == world->vendings.end() || vend->count == 0))
    {
        if (!sellable(id)) throw std::runtime_error("You can't put that in a Vending Machine.");
        const int have = pPeer->have_slot(::slot(id, 0));
        if (have <= 0) return;

        if (vend == world->vendings.end()) vend = world->vendings.emplace(world->vendings.end(), ::vending{.pos = pos});
        if (vend->id != id) vend->price = 0; // @note a new item needs a new price
        vend->id = id;
        vend->count = have; // @note the whole stack goes in, like real Growtopia
        modify_item_inventory(event, ::slot(id, static_cast<short>(-have)));
        on::ConsoleMessage(event.peer, std::format("`7[```9You put {} {} in the machine.```7]``", have, id_to_item(id).raw_name));
        update_tile(event, *world, pos);
        vending_wrench(event, *world, pos); // @note straight on to setting a price
        return;
    }
    if (vend == world->vendings.end()) return;

    if (button == "pullstock" && vend->count > 0)
    {
        const int room = pPeer->can_hold(vend->id, 1) ? 200 - pPeer->have_slot(::slot(vend->id, 0)) : 0;
        const int taken = std::min(room, vend->count);
        if (taken <= 0) throw std::runtime_error("You don't have room in your backpack!");
        modify_item_inventory(event, ::slot(vend->id, static_cast<short>(taken)));
        vend->count -= taken;
        on::ConsoleMessage(event.peer, std::format("`7[```9You took {} {} out of the machine.```7]``", taken, id_to_item(vend->id).raw_name));
        update_tile(event, *world, pos);
        return;
    }
    if (button == "addstock" && vend->count > 0)
    {
        const int have = pPeer->have_slot(::slot(vend->id, 0));
        if (have <= 0) return;
        modify_item_inventory(event, ::slot(vend->id, static_cast<short>(-have)));
        vend->count += have;
        on::ConsoleMessage(event.peer, std::format("`7[```9You put {} more {} in the machine.```7]``", have, id_to_item(vend->id).raw_name));
        return;
    }
    if (button == "withdraw" && vend->earned > 0)
    {
        const int before = vend->earned;
        vend->earned = give_locks(event, pPeer, vend->earned);
        if (vend->earned == before) throw std::runtime_error("You don't have room in your backpack!");
        on::ConsoleMessage(event.peer, std::format("`7[```9You collected {} from the machine.```7]``", locks_text(before - vend->earned)));
        return;
    }
    if (vend->count > 0 && !hPipe["setprice"].empty())
    {
        const int value = std::clamp(atoi(hPipe["setprice"].c_str()), 0, 20000);
        const bool per_lock = hPipe["chk_perlock"] == "1" && hPipe["chk_peritem"] != "1";
        if (per_lock && value > 200) throw std::runtime_error("You can sell at most 200 items per World Lock.");
        const int price = per_lock ? -value : value;
        if (price == vend->price) return;

        vend->price = price;
        const std::string text = (price == 0) ? "The machine is no longer selling." :
            (price > 0) ? std::format("The price is now `5{}`` per item.", locks_text(price)) :
                          std::format("The price is now `w{}`` items per `5World Lock``.", -price);
        on::ConsoleMessage(event.peer, std::format("`7[```9{}```7]``", text));
        update_tile(event, *world, pos);
    }
}

void vending_buy(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::world *world = peer_world(pPeer);
    ::pos pos{};
    if (world == nullptr || !read_tile(hPipe, *world, type::VENDING_MACHINE, pos)) return;

    auto vend = std::ranges::find(world->vendings, pos, &::vending::pos);
    if (vend == world->vendings.end() || vend->count <= 0 || vend->price == 0) throw std::runtime_error("This machine is out of order.");
    /* @note the owner may have changed the deal while the buyer was looking */
    if (atoi(hPipe["expectprice"].c_str()) != vend->price || atoi(hPipe["expectitem"].c_str()) != vend->id)
        throw std::runtime_error("The price or item has changed! Look again before you buy.");

    const int asked = atoi(hPipe["buycount"].c_str());
    if (asked <= 0) return;

    int amount{}, cost{};
    if (vend->price > 0) { amount = asked; cost = asked * vend->price; }
    else                 { cost = asked; amount = asked * -vend->price; }
    if (amount > vend->count) throw std::runtime_error(std::format("The machine only has {} left.", vend->count));
    if (amount > 200 || !pPeer->can_hold(vend->id, amount)) throw std::runtime_error("You don't have room in your backpack!");

    const int wls = pPeer->have_slot(::slot(WORLD_LOCK, 0)), dls = pPeer->have_slot(::slot(DIAMOND_LOCK, 0));
    if (wls + dls * 100 < cost) throw std::runtime_error(std::format("You can't afford that! It costs `5{}``.", locks_text(cost)));

    /* @note pay with World Locks first, then break Diamond Locks and give back the change */
    if (wls >= cost) modify_item_inventory(event, ::slot(WORLD_LOCK, static_cast<short>(-cost)));
    else
    {
        const int short_by = cost - wls;
        const int dls_used = (short_by + 99) / 100;
        const int change = dls_used * 100 - short_by;
        if (wls > 0) modify_item_inventory(event, ::slot(WORLD_LOCK, static_cast<short>(-wls)));
        modify_item_inventory(event, ::slot(DIAMOND_LOCK, static_cast<short>(-dls_used)));
        if (change > 0) give_locks(event, pPeer, change);
    }
    modify_item_inventory(event, ::slot(vend->id, static_cast<short>(amount)));
    vend->count -= amount;
    vend->earned += cost;

    const std::string message = std::format("`7[```9{} bought {} {} for {}.```7]``", pPeer->growid, amount, id_to_item(vend->id).raw_name, locks_text(cost));
    peers(world->name, PeerSearch::WORLD, [&](ENetPeer *peer) { on::ConsoleMessage(peer, message); });
    send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, message, 0u, 1u });
    pPeer->mysql_update_all(); // @note the purchase is saved right away, like trades
    if (vend->count == 0) update_tile(event, *world, pos);
}

/* ---------------- Storage Box Xtreme ---------------- */

void storage_wrench(ENetEvent &event, ::world &world, const ::pos &pos)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const ::item &box_item = id_to_item(world.blocks[cord(pos.x_int(), pos.y_int())].fg);

    if (world.owner == 0) throw std::runtime_error("This only works in World-Locked worlds.");
    if (world.owner != pPeer->user_id && pPeer->role < MODERATOR) throw std::runtime_error("Only the World Lock owner can use this.");

    auto box = std::ranges::find(world.storages, pos, &::storage::pos);
    const int capacity = storage_capacity(box_item.id);
    const int used = (box != world.storages.end()) ? static_cast<int>(box->items.size()) : 0;

    std::string rows{};
    if (box != world.storages.end())
        for (std::size_t i = 0; i < box->items.size(); ++i)
            rows += std::format("add_button_with_icon|take_{}||staticBlueFrame|{}|{}|\n", i, box->items[i].id, box->items[i].count);
    if (!rows.empty()) rows += "add_button_with_icon||END_LIST|noflags|0||\n";

    send_varlist(event.peer, {
        "OnDialogRequest",
        std::format(
            "set_default_color|`o\n"
            "add_label_with_icon|big|`w{}``|left|{}|\n"
            "add_spacer|small|\n"
            "add_textbox|`w{}/{}`` stacks stored. Tap a stack to take it out.|left|\n"
            "{}"
            "add_spacer|small|\n"
            "add_item_picker|itemid|`wDeposit item``|Choose an item to put in the box!|\n"
            "embed_data|tilex|{}\n"
            "embed_data|tiley|{}\n"
            "end_dialog|storage|Close||\n",
            box_item.raw_name, box_item.id, used, capacity, rows, pos.x_int(), pos.y_int()
        )
    });
}

void storage_edit(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::world *world = peer_world(pPeer);
    ::pos pos{};
    if (world == nullptr || !read_tile(hPipe, *world, type::STORAGE_BOX, pos)) return;
    if (world->owner != pPeer->user_id && pPeer->role < MODERATOR) return;

    if (const short id = static_cast<short>(atoi(hPipe["itemid"].c_str())); id > 0)
    {
        if (id_to_item(id).cat & CAT_UNTRADEABLE) throw std::runtime_error("Untradeable items can't be placed here.");
        const int have = pPeer->have_slot(::slot(id, 0));
        if (have <= 0) return;
        const ::item &item = id_to_item(id);
        send_varlist(event.peer, {
            "OnDialogRequest",
            ::create_dialog()
                .set_default_color("`o")
                .add_label_with_icon("big", std::format("`wDeposit {}``", item.raw_name), item.id)
                .add_text_input("count", "How many?", static_cast<short>(have), 5)
                .embed_data("itemid", id)
                .embed_data("tilex", pos.x_int())
                .embed_data("tiley", pos.y_int())
                .end_dialog("storage_deposit", "Cancel", "Deposit")
        });
        return;
    }
    const std::string button = hPipe["buttonClicked"];
    if (button.starts_with("take_"))
    {
        auto box = std::ranges::find(world->storages, pos, &::storage::pos);
        const std::size_t index = static_cast<std::size_t>(atoi(button.substr(5).c_str()));
        if (box == world->storages.end() || index >= box->items.size()) return;
        const ::slot &slot = box->items[index];
        const ::item &item = id_to_item(slot.id);
        send_varlist(event.peer, {
            "OnDialogRequest",
            ::create_dialog()
                .set_default_color("`o")
                .add_label_with_icon("big", std::format("`wWithdraw {}``", item.raw_name), item.id)
                .add_textbox(std::format("The box holds `w{}``.", slot.count))
                .add_text_input("count", "How many?", slot.count, 5)
                .embed_data("itemid", slot.id)
                .embed_data("tilex", pos.x_int())
                .embed_data("tiley", pos.y_int())
                .end_dialog("storage_withdraw", "Cancel", "Withdraw")
        });
    }
}

void storage_deposit(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::world *world = peer_world(pPeer);
    ::pos pos{};
    if (world == nullptr || !read_tile(hPipe, *world, type::STORAGE_BOX, pos)) return;
    if (world->owner != pPeer->user_id && pPeer->role < MODERATOR) return;

    const short id = static_cast<short>(atoi(hPipe["itemid"].c_str()));
    if (id <= 0 || (id_to_item(id).cat & CAT_UNTRADEABLE)) return;
    const int count = std::min(atoi(hPipe["count"].c_str()), static_cast<int>(pPeer->have_slot(::slot(id, 0))));
    if (count <= 0) return;

    auto box = std::ranges::find(world->storages, pos, &::storage::pos);
    if (box == world->storages.end()) box = world->storages.emplace(world->storages.end(), ::storage{.pos = pos});

    const int capacity = storage_capacity(world->blocks[cord(pos.x_int(), pos.y_int())].fg);
    int left = count;
    for (::slot &slot : box->items) // @note top up stacks of the same item first
    {
        if (slot.id != id || slot.count >= 200) continue;
        const int add = std::min(left, 200 - slot.count);
        slot.count += add, left -= add;
    }
    while (left > 0 && static_cast<int>(box->items.size()) < capacity)
    {
        const int add = std::min(left, 200);
        box->items.emplace_back(id, static_cast<short>(add));
        left -= add;
    }
    const int stored = count - left;
    if (stored <= 0) throw std::runtime_error("The box is full!");

    modify_item_inventory(event, ::slot(id, static_cast<short>(-stored)));
    on::ConsoleMessage(event.peer, std::format("`7[```9Stored {} {} in the box.```7]``", stored, id_to_item(id).raw_name));
    pPeer->mysql_update_all();
    world->save();
}

void storage_withdraw(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::world *world = peer_world(pPeer);
    ::pos pos{};
    if (world == nullptr || !read_tile(hPipe, *world, type::STORAGE_BOX, pos)) return;
    if (world->owner != pPeer->user_id && pPeer->role < MODERATOR) return;

    auto box = std::ranges::find(world->storages, pos, &::storage::pos);
    if (box == world->storages.end()) return;
    const short id = static_cast<short>(atoi(hPipe["itemid"].c_str()));

    int stored = 0;
    for (const ::slot &slot : box->items) if (slot.id == id) stored += slot.count;
    const int room = pPeer->can_hold(id, 1) ? 200 - pPeer->have_slot(::slot(id, 0)) : 0;
    int count = std::min({atoi(hPipe["count"].c_str()), stored, room});
    if (count <= 0) throw std::runtime_error((room <= 0) ? "You don't have room in your backpack!" : "There's nothing to take.");

    const int taken = count;
    for (auto it = box->items.rbegin(); it != box->items.rend() && count > 0; ++it) // @note take from the last stacks first
    {
        if (it->id != id) continue;
        const int take = std::min(count, static_cast<int>(it->count));
        it->count -= take, count -= take;
    }
    std::erase_if(box->items, [](const ::slot &slot) { return slot.count <= 0; });

    modify_item_inventory(event, ::slot(id, static_cast<short>(taken)));
    on::ConsoleMessage(event.peer, std::format("`7[```9Took {} {} out of the box.```7]``", taken, id_to_item(id).raw_name));
    pPeer->mysql_update_all();
    world->save();
}

/* ---------------- Display Block ---------------- */

void display_wrench(ENetEvent &event, ::world &world, const ::pos &pos)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const ::item &block_item = id_to_item(world.blocks[cord(pos.x_int(), pos.y_int())].fg);
    auto display = std::ranges::find(world.displays, pos, &::display::pos);
    const bool has_item = display != world.displays.end() && display->id != 0;
    const bool owner = world.can_edit(*pPeer, pos, false);

    ::create_dialog dialog = ::create_dialog()
        .set_default_color("`o")
        .add_label_with_icon("big", std::format("`w{}``", block_item.raw_name), block_item.id)
        .add_spacer("small")
        .embed_data("tilex", pos.x_int())
        .embed_data("tiley", pos.y_int());

    if (has_item)
    {
        const ::item &item = id_to_item(static_cast<u_short>(display->id));
        dialog.add_label_with_icon("small", std::format("A `w{}`` is on display here.", item.raw_name), item.id);
        if (owner) dialog.add_button("remove", "`wTake it out``");
    }
    else if (owner)
    {
        if (world.owner == 0) throw std::runtime_error("This only works in World-Locked worlds.");
        dialog.add_textbox("This Display Block is empty.")
              .add_item_picker("displayitem", "`wDisplay an item``", "Choose an item to put on display!");
    }
    else return;

    send_varlist(event.peer, { "OnDialogRequest", dialog.add_spacer("small").end_dialog("display", "Close", "") });
}

void display_edit(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::world *world = peer_world(pPeer);
    ::pos pos{};
    if (world == nullptr || !read_tile(hPipe, *world, type::DISPLAY_BLOCK, pos)) return;
    if (!world->can_edit(*pPeer, pos, false)) return;

    auto display = std::ranges::find(world->displays, pos, &::display::pos);
    if (hPipe["buttonClicked"] == "remove")
    {
        if (display == world->displays.end() || display->id == 0) return;
        const short id = static_cast<short>(display->id);
        if (!pPeer->can_hold(id, 1)) throw std::runtime_error("You don't have room in your backpack!");
        modify_item_inventory(event, ::slot(id, 1));
        display->id = 0;
        update_tile(event, *world, pos);
        return;
    }
    const short id = static_cast<short>(atoi(hPipe["displayitem"].c_str()));
    if (id <= 0 || (display != world->displays.end() && display->id != 0)) return;
    if (world->owner == 0) return;
    if (id_to_item(id).cat & CAT_UNTRADEABLE) throw std::runtime_error("You can't display untradeable items.");
    if (pPeer->have_slot(::slot(id, 0)) <= 0) return;

    modify_item_inventory(event, ::slot(id, -1));
    if (display == world->displays.end()) world->displays.emplace_back(static_cast<u_int>(id), pos);
    else display->id = static_cast<u_int>(id);
    update_tile(event, *world, pos);
}
