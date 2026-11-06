#include "pch.hpp"

#include "onVariant/ConsoleMessage.hpp"
#include "tools/create_dialog.hpp"
#include "tools/sound.hpp"
#include "daily_quest.hpp"

#include "wrenchables.hpp"

#include <algorithm>

/*
* @note Telephone, Mailbox, Bulletin Board and Donation Box, following real Growtopia.
* texts and limits from github.com/cppdecl/Nomikai (BlockWrenchListener.hh, DialogCloseListener.hh)
*/

namespace
{
    constexpr short TELEPHONE = 3898;
    constexpr short CRAZY_JIM = 3902;
    constexpr short DIAMOND_LOCK = 1796;
    constexpr short BLUE_GEM_LOCK = 7188;
    constexpr short WORLD_KEY = 1424;

    constexpr std::size_t MAX_LETTERS = 20;
    constexpr std::size_t MAX_GIFTS = 20;
    constexpr int MAX_GIFTS_PER_PLAYER = 3;

    ::world *peer_world(::peer *pPeer)
    {
        if (pPeer->netid == 0) return nullptr;
        auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
        return (world != worlds.end()) ? &*world : nullptr;
    }

    /* @return true if tilex|tiley holds a block of this action type */
    bool read_tile(const ::hPipe &hPipe, const ::world &world, u_char block_type, ::pos &pos)
    {
        pos = ::pos{atoi(hPipe["tilex"].c_str()), atoi(hPipe["tiley"].c_str())};
        if (!valid_tile(world, pos)) return false;
        return id_to_item(world.blocks[cord(pos.x_int(), pos.y_int())].fg).type == block_type;
    }

    /* @brief keep typed text from breaking the dialog: no '|' or new lines, and a length cap */
    std::string clean(std::string text, std::size_t max)
    {
        std::erase_if(text, [](char c) { return c == '|' || c == '\n' || c == '\r' || c == '\0'; });
        if (text.size() > max) text.resize(max);
        return text;
    }

    void update_tile(ENetEvent &event, ::world &world, const ::pos &pos)
    {
        ::block &block = world.blocks[cord(pos.x_int(), pos.y_int())];
        send_tile_update(event, ::gamePacket{.id = block.fg, .punch = pos}, block, world);
    }

    /* @brief the flag on a Mailbox goes up (and a Donation Box looks full) while something is inside */
    void set_full(ENetEvent &event, ::world &world, const ::pos &pos, bool full)
    {
        ::block &block = world.blocks[cord(pos.x_int(), pos.y_int())];
        const bool was = block.state[2] & S_TOGGLE;
        if (was == full) return;
        if (full) block.state[2] |= S_TOGGLE;
        else block.state[2] &= ~S_TOGGLE;
        update_tile(event, world, pos);
    }

    void tell_world(const ::world &world, int netid, const std::string &message)
    {
        peers(world.name, PeerSearch::WORLD, [&](ENetPeer *peer)
        {
            send_varlist(peer, { "OnTalkBubble", netid, message, 0u, 0u });
            on::ConsoleMessage(peer, message);
        });
    }

    ::mailbox &mailbox_at(::world &world, const ::pos &pos)
    {
        auto box = std::ranges::find(world.mailboxes, pos, &::mailbox::pos);
        if (box == world.mailboxes.end()) box = world.mailboxes.emplace(world.mailboxes.end(), ::mailbox{.pos = pos});
        return *box;
    }

    ::donation_box &donation_at(::world &world, const ::pos &pos)
    {
        auto box = std::ranges::find(world.donation_boxes, pos, &::donation_box::pos);
        if (box == world.donation_boxes.end()) box = world.donation_boxes.emplace(world.donation_boxes.end(), ::donation_box{.pos = pos});
        return *box;
    }

    /* @return a letter's line. owners get a button on each to throw it away */
    std::string letter_line(const ::letter &letter, bool hide_names, bool button)
    {
        const std::string text = hide_names ? std::format("`2{}``", letter.text) : std::format("`w{}``: `2{}``", letter.name, letter.text);
        if (button) return std::format("add_label_with_icon_button|small|{}|left|660|del_{}|\n", text, letter.id);
        return std::format("add_label_with_icon|small|{}|left|660|\n", text);
    }

    /* ---------------- Telephone ---------------- */

    void send_phone(ENetEvent &event, const ::pos &pos, ::create_dialog &dialog)
    {
        dialog.embed_data("tilex", pos.x_int())
              .embed_data("tiley", pos.y_int());
        send_varlist(event.peer, { "OnDialogRequest", dialog.end_dialog("telephone", "Hang Up", "") });
    }

    void crazy_jim(ENetEvent &event, const ::pos &pos)
    {
        ::create_dialog dialog = ::create_dialog()
            .set_default_color("`o")
            .add_label_with_icon("big", "`wCrazy Jim's Quest Emporium``", CRAZY_JIM)
            .add_textbox("HEEEEYYY there Growtopian! I'm Crazy Jim, and my quests are so crazy they're KERRRRAAAAZZY!! And that is clearly very crazy, so please, be cautious around them. What can I do ya for, partner?")
            .add_spacer("small")
            .add_button("jim_daily", "Daily Quest");
        send_phone(event, pos, dialog);
    }

    void sales_man(ENetEvent &event, const ::pos &pos)
    {
        ::create_dialog dialog = ::create_dialog()
            .set_default_color("`o")
            .add_label_with_icon("big", "`wSales-Man``", BLUE_GEM_LOCK)
            .add_textbox("It is I, Sales-Man, savior of the wealthy! Let me rescue you from your riches. What would you like to buy today?")
            .add_spacer("small")
            .add_button("sales_bgl", "Blue Gem Lock");
        send_phone(event, pos, dialog);
    }

    void sales_bgl(ENetEvent &event, const ::pos &pos)
    {
        ::peer *pPeer = static_cast<::peer*>(event.peer->data);
        const int dls = pPeer->have_slot(::slot(DIAMOND_LOCK, 0));
        ::create_dialog dialog = ::create_dialog()
            .set_default_color("`o")
            .add_label_with_icon("big", "`wSales-Man``", BLUE_GEM_LOCK);
        if (dls >= 100)
        {
            dialog.add_textbox("Excellent! I'm happy to sell you a `2Blue Gem Lock`` in exchange for `2100 Diamond Locks``.")
                  .add_smalltext(std::format("You have {} Diamond Locks.", dls))
                  .add_spacer("small")
                  .add_button("sales_bgl_buy", "Thank you!");
        }
        else
        {
            dialog.add_textbox(std::format("A `2Blue Gem Lock`` costs `2100 Diamond Locks``, and you only have {}. Come back when you're richer!", dls))
                  .add_spacer("small");
        }
        dialog.add_button("sales_menu", "Back");
        send_phone(event, pos, dialog);
    }

    void dial(ENetEvent &event, const ::pos &pos, const std::string &number)
    {
        if (number == "12345") { crazy_jim(event, pos); return; }
        if (number == "53785") { sales_man(event, pos); return; }

        ::create_dialog dialog = ::create_dialog()
            .set_default_color("`o")
            .add_label_with_icon("big", "`wTelephone``", TELEPHONE)
            .add_textbox((number.size() == 5) ?
                "`4The number you have tried to reach is disconnected. Please check yourself before you wreck yourself.``" :
                "Phone numbers have 5 digits, like 12345.")
            .add_spacer("small")
            .add_text_input("dial", "Phone #", "", 5);
        dialog.embed_data("tilex", pos.x_int())
              .embed_data("tiley", pos.y_int());
        send_varlist(event.peer, { "OnDialogRequest", dialog.end_dialog("telephone", "Hang Up", "Dial") });
    }
}

void telephone_wrench(ENetEvent &event, ::world &world, const ::pos &pos)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (!world.can_edit(*pPeer, pos, false))
    {
        play_sfx(event.peer, "audio/cant_place_tile.wav");
        return;
    }
    ::create_dialog dialog = ::create_dialog()
        .set_default_color("`o")
        .add_label_with_icon("big", "`wTelephone``", TELEPHONE)
        .add_textbox("Dial a number to call somebody in Growtopia. Phone numbers have 5 digits, like 12345 (try it - you'd be crazy not to!). Most numbers are not in service!")
        .add_spacer("small")
        .add_text_input("dial", "Phone #", "", 5);
    dialog.embed_data("tilex", pos.x_int())
          .embed_data("tiley", pos.y_int());
    send_varlist(event.peer, { "OnDialogRequest", dialog.end_dialog("telephone", "Hang Up", "Dial") });
}

void telephone(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::world *world = peer_world(pPeer);
    ::pos pos{};
    if (world == nullptr) return;
    pos = ::pos{atoi(hPipe["tilex"].c_str()), atoi(hPipe["tiley"].c_str())};
    if (!valid_tile(*world, pos) || world->blocks[cord(pos.x_int(), pos.y_int())].fg != TELEPHONE) return;
    if (!world->can_edit(*pPeer, pos, false)) return;

    const std::string button = hPipe["buttonClicked"];
    if (button == "jim_menu") crazy_jim(event, pos);
    else if (button == "jim_daily") show_daily_quest(event, &pos);
    else if (button == "deliver")
    {
        deliver_daily_quest(event);
        show_daily_quest(event, &pos);
    }
    else if (button == "sales_menu") sales_man(event, pos);
    else if (button == "sales_bgl") sales_bgl(event, pos);
    else if (button == "sales_bgl_buy")
    {
        if (pPeer->have_slot(::slot(DIAMOND_LOCK, 0)) < 100) return;
        if (!pPeer->can_hold(BLUE_GEM_LOCK, 1)) throw std::runtime_error("`4That won't fit in your inventory.``");
        modify_item_inventory(event, ::slot(DIAMOND_LOCK, -100));
        modify_item_inventory(event, ::slot(BLUE_GEM_LOCK, 1));
        const std::string message = "You converted `w100 Diamond Locks`` into `w1 Blue Gem Lock``.";
        send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, message, 0u, 1u });
        on::ConsoleMessage(event.peer, message);
        play_in_world(world->name, pPeer->netid, "audio/piano_nice.wav");
        pPeer->mysql_update_all();
    }
    else if (button.empty() || button == "Dial") // @note the Dial button
    {
        std::string number = hPipe["dial"];
        std::erase_if(number, [](char c) { return c < '0' || c > '9'; });
        dial(event, pos, number);
    }
}

/* ---------------- Mailbox ---------------- */

void mailbox_wrench(ENetEvent &event, ::world &world, const ::pos &pos)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const ::item &item = id_to_item(world.blocks[cord(pos.x_int(), pos.y_int())].fg);
    auto box = std::ranges::find(world.mailboxes, pos, &::mailbox::pos);
    const std::size_t count = (box != world.mailboxes.end()) ? box->letters.size() : 0;

    ::create_dialog dialog = ::create_dialog()
        .set_default_color("`o")
        .add_label_with_icon("big", std::format("`w{}``", item.raw_name), item.id)
        .add_quick_exit();

    if (world.can_edit(*pPeer, pos, false))
    {
        if (count == 0) dialog.add_textbox("Your mailbox is currently empty.");
        else
        {
            dialog.add_textbox(std::format("You have `w{}`` letters:", count)).add_spacer("small");
            std::string rows{};
            for (const ::letter &letter : box->letters) rows += letter_line(letter, false, true);
            dialog.add_raw(rows).add_spacer("small")
                  .add_button("clear", "`4Empty Mailbox``");
        }
        dialog.add_spacer("small").add_textbox("Write a letter to yourself?");
    }
    else dialog.add_textbox("Want to leave a message for the owner?");

    dialog.add_text_input("message", "", "", 128)
          .add_spacer("small")
          .add_button("send", "`2Send Letter``")
          .add_spacer("small");
    dialog.embed_data("tilex", pos.x_int())
          .embed_data("tiley", pos.y_int());
    send_varlist(event.peer, { "OnDialogRequest", dialog.end_dialog("mailbox", "Cancel", "OK") });
}

void mailbox_edit(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::world *world = peer_world(pPeer);
    ::pos pos{};
    if (world == nullptr || !read_tile(hPipe, *world, type::MAILBOX, pos)) return;
    const bool owner = world->can_edit(*pPeer, pos, false);
    const std::string button = hPipe["buttonClicked"];

    if (button == "send")
    {
        const std::string message = clean(hPipe["message"], 128);
        if (message.size() < 3) throw std::runtime_error("That's not interesting enough to mail.");
        ::mailbox &box = mailbox_at(*world, pos);
        if (box.letters.size() >= MAX_LETTERS) throw std::runtime_error("The mailbox already has `w20`` letters in it. Try again later.");
        box.letters.push_back(::letter{box.next_id++, pPeer->user_id, pPeer->growid, message});
        send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, "`2You place your letter in the mailbox.``", 0u, 1u });
        play_sfx(event.peer, "audio/page_turn.wav");
        set_full(event, *world, pos, true);
        world->save();
        return;
    }
    if (!owner) return;
    auto box = std::ranges::find(world->mailboxes, pos, &::mailbox::pos);
    if (box == world->mailboxes.end()) return;

    if (button == "clear")
    {
        box->letters.clear();
        send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, "`2Cleared mailbox.``", 0u, 1u });
    }
    else if (button.starts_with("del_"))
    {
        const u_int id = static_cast<u_int>(atoll(button.substr(4).c_str()));
        if (std::erase_if(box->letters, [id](const ::letter &l) { return l.id == id; }) == 0) return;
        send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, "`2Removed mail.``", 0u, 1u });
    }
    else return;
    play_sfx(event.peer, "audio/page_turn.wav");
    set_full(event, *world, pos, !box->letters.empty());
    world->save();
}

/* ---------------- Bulletin Board ---------------- */

void bulletin_wrench(ENetEvent &event, ::world &world, const ::pos &pos)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const ::item &item = id_to_item(world.blocks[cord(pos.x_int(), pos.y_int())].fg);
    auto box = std::ranges::find(world.mailboxes, pos, &::mailbox::pos);
    const bool owner = world.can_edit(*pPeer, pos, false);
    const bool is_public = box != world.mailboxes.end() && box->is_public;
    const bool hide_names = box != world.mailboxes.end() && box->hide_names;

    ::create_dialog dialog = ::create_dialog()
        .set_default_color("`o")
        .add_label_with_icon("big", std::format("`w{}``", item.raw_name), item.id)
        .add_spacer("small")
        .add_quick_exit();

    if (box == world.mailboxes.end() || box->letters.empty()) dialog.add_textbox(std::format("The {} is empty.", item.raw_name));
    else
    {
        std::string rows{};
        for (const ::letter &letter : box->letters) rows += letter_line(letter, hide_names, owner);
        dialog.add_raw(rows);
    }
    dialog.add_spacer("small");
    if (owner || is_public)
    {
        dialog.add_textbox("Add to conversation?")
              .add_text_input("message", "", "", 128)
              .add_spacer("small")
              .add_button("send", "`2Add``")
              .add_spacer("small");
    }
    if (owner)
    {
        dialog.add_label_with_icon("big", "`wOwner Options``", 242)
              .add_spacer("small")
              .add_button("clear", "`4Clear Board``")
              .add_checkbox("is_public", "Public can add", is_public)
              .add_checkbox("hide_names", "Hide names", hide_names);
    }
    dialog.embed_data("tilex", pos.x_int())
          .embed_data("tiley", pos.y_int());
    send_varlist(event.peer, { "OnDialogRequest", dialog.end_dialog("bulletin", "Cancel", "OK") });
}

void bulletin_edit(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::world *world = peer_world(pPeer);
    ::pos pos{};
    if (world == nullptr || !read_tile(hPipe, *world, type::BULLETIN, pos)) return;
    const bool owner = world->can_edit(*pPeer, pos, false);
    ::mailbox &box = mailbox_at(*world, pos);
    const std::string button = hPipe["buttonClicked"];

    if (button == "send")
    {
        if (!owner && !box.is_public) return;
        const std::string message = clean(hPipe["message"], 128);
        if (message.size() < 3) throw std::runtime_error("That message isn't interesting enough.");
        if (box.letters.size() >= MAX_LETTERS) box.letters.erase(box.letters.begin()); // @note the oldest makes room
        box.letters.push_back(::letter{box.next_id++, pPeer->user_id, pPeer->growid, message});
        send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, "`2Bulletin posted.``", 0u, 1u });
        play_sfx(event.peer, "audio/page_turn.wav");
    }
    else if (!owner) return;
    else if (button == "clear")
    {
        box.letters.clear();
        send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, "`2Cleared bulletin.``", 0u, 1u });
        play_sfx(event.peer, "audio/page_turn.wav");
    }
    else if (button.starts_with("del_"))
    {
        const u_int id = static_cast<u_int>(atoll(button.substr(4).c_str()));
        if (std::erase_if(box.letters, [id](const ::letter &l) { return l.id == id; }) == 0) return;
        send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, "`2Removed bulletin entry.``", 0u, 1u });
        play_sfx(event.peer, "audio/page_turn.wav");
    }
    if (owner && !hPipe["is_public"].empty()) // @note the owner's checkboxes come with every button
    {
        box.is_public = hPipe["is_public"] == "1";
        box.hide_names = hPipe["hide_names"] == "1";
    }
    world->save();
}

/* ---------------- Donation Box ---------------- */

void donation_wrench(ENetEvent &event, ::world &world, const ::pos &pos)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const ::item &item = id_to_item(world.blocks[cord(pos.x_int(), pos.y_int())].fg);
    auto box = std::ranges::find(world.donation_boxes, pos, &::donation_box::pos);
    const std::size_t count = (box != world.donation_boxes.end()) ? box->gifts.size() : 0;

    ::create_dialog dialog = ::create_dialog()
        .set_default_color("`o")
        .add_label_with_icon("big", std::format("`w{}``", item.raw_name), item.id);

    if (world.can_edit(*pPeer, pos, false))
    {
        if (count == 0) dialog.add_textbox("The box is currently empty.");
        else
        {
            dialog.add_textbox(std::format("You have {} gifts waiting:", count)).add_spacer("small");
            for (const ::gift &gift : box->gifts)
            {
                const std::string name = id_to_item(gift.item).raw_name;
                dialog.add_checkbox(std::format("_dn{}", gift.id), gift.note.empty() ?
                    std::format("{} (`w{}``) from `w{}``", name, gift.count, gift.name) :
                    std::format("{} (`w{}``) from `w{}```#- \"{}\"``", name, gift.count, gift.name, gift.note), false);
            }
            dialog.add_spacer("small")
                  .add_button("take_selected", "`4Retrieve Selected``")
                  .add_button("take_all", "`4Retrieve All``");
        }
    }
    else
    {
        if (count == 0) dialog.add_textbox("The box is currently empty.");
        else dialog.add_textbox(std::format("You see `w{}`` gifts in the box.", count));
        dialog.add_textbox("Would you like to leave a `9gift`` for the owner?");
    }
    dialog.add_spacer("small")
          .add_item_picker("donate_item", "`wGive Gift`` (Min rarity: `52``)", "Choose an item to donate");
    dialog.embed_data("tilex", pos.x_int())
          .embed_data("tiley", pos.y_int());
    send_varlist(event.peer, { "OnDialogRequest", dialog.end_dialog("donation", "Cancel", "") });
}

namespace
{
    /* @return why this gift can't go in, or empty if it can */
    std::string gift_problem(const ::peer &peer, ::world &world, const ::pos &pos, short id)
    {
        auto box = std::ranges::find(world.donation_boxes, pos, &::donation_box::pos);
        if (box != world.donation_boxes.end())
        {
            if (box->gifts.size() >= MAX_GIFTS) return "The donation box is already full!";
            const auto mine = std::ranges::count_if(box->gifts, [&](const ::gift &g) { return g.uid == peer.user_id; });
            if (mine >= MAX_GIFTS_PER_PLAYER && !world.can_edit(peer, pos, false)) return "You already donated items 3 times! Try again later.";
        }
        const ::item &item = id_to_item(id);
        if (item.id == 0 || item.type == type::FIST || item.type == type::WRENCH || item.id == WORLD_KEY) return "You can't put that there.";
        if (item.cat & CAT_UNTRADEABLE) return "That's too special to put in there!";
        if (item.rarity < 2) return "This box only accepts items of rarity 2 and above.";
        return "";
    }

    /* @brief give the owner a gift. @return false if it doesn't fit */
    bool take_gift(ENetEvent &event, ::world &world, const ::gift &gift)
    {
        ::peer *pPeer = static_cast<::peer*>(event.peer->data);
        const ::item &item = id_to_item(gift.item);
        if (!pPeer->can_hold(gift.item, gift.count))
        {
            on::ConsoleMessage(event.peer, std::format("`${}`` `${}`` `4won't `ofit in my inventory.", gift.count, item.raw_name));
            return false;
        }
        modify_item_inventory(event, ::slot(gift.item, gift.count));
        tell_world(world, pPeer->netid, std::format("`7[```5[```w{}`w picked up `5{}`` `2{}`` from {}``, how nice!`5]```7]``",
            pPeer->growid, gift.count, item.raw_name, gift.name));
        return true;
    }
}

void donation_edit(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::world *world = peer_world(pPeer);
    ::pos pos{};
    if (world == nullptr || !read_tile(hPipe, *world, type::DONATION_BOX, pos)) return;

    if (const short id = static_cast<short>(atoi(hPipe["donate_item"].c_str())); id > 0)
    {
        if (pPeer->have_slot(::slot(id, 0)) <= 0) return;
        if (const std::string problem = gift_problem(*pPeer, *world, pos, id); !problem.empty()) throw std::runtime_error(problem);
        const ::item &item = id_to_item(id);
        ::create_dialog dialog = ::create_dialog()
            .set_default_color("`o")
            .add_label_with_icon("big", std::format("`w{}``", item.raw_name), item.id)
            .add_textbox("How many to put in the box as a gift? (Note: You will `4LOSE`` the items you give!)")
            .add_text_input("count", "Count:", static_cast<short>(pPeer->have_slot(::slot(id, 0))), 3)
            .add_text_input("note", "Optional Note:", "", 128)
            .add_spacer("small")
            .add_button("give", "`4Give the item(s)``")
            .add_spacer("small")
            .add_button("cancel", "`wCancel``");
        dialog.embed_data("itemid", id)
              .embed_data("tilex", pos.x_int())
              .embed_data("tiley", pos.y_int());
        send_varlist(event.peer, { "OnDialogRequest", dialog.end_dialog("donation_give", "", "") });
        return;
    }
    if (!world->can_edit(*pPeer, pos, false)) return;
    auto box = std::ranges::find(world->donation_boxes, pos, &::donation_box::pos);
    if (box == world->donation_boxes.end()) return;

    const std::string button = hPipe["buttonClicked"];
    if (button != "take_all" && button != "take_selected") return;
    std::vector<::gift> gifts = box->gifts; // @note giving items can send packets; work on a copy
    std::vector<u_int> taken{};
    for (const ::gift &gift : gifts)
    {
        if (button == "take_selected" && hPipe[std::format("_dn{}", gift.id)] != "1") continue;
        if (take_gift(event, *world, gift)) taken.push_back(gift.id);
    }
    box = std::ranges::find(world->donation_boxes, pos, &::donation_box::pos);
    if (box == world->donation_boxes.end()) return;
    std::erase_if(box->gifts, [&](const ::gift &g) { return std::ranges::find(taken, g.id) != taken.end(); });
    play_sfx(event.peer, "audio/page_turn.wav");
    set_full(event, *world, pos, !box->gifts.empty());
    pPeer->mysql_update_all();
    world->save();
}

void donation_give(ENetEvent &event, const ::hPipe &hPipe)
{
    if (hPipe["buttonClicked"] != "give") return;
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::world *world = peer_world(pPeer);
    ::pos pos{};
    if (world == nullptr || !read_tile(hPipe, *world, type::DONATION_BOX, pos)) return;

    const short id = static_cast<short>(atoi(hPipe["itemid"].c_str()));
    if (id <= 0) return;
    const int count = std::min(atoi(hPipe["count"].c_str()), static_cast<int>(pPeer->have_slot(::slot(id, 0))));
    if (count <= 0) throw std::runtime_error("You can't donate nothing!");
    if (const std::string problem = gift_problem(*pPeer, *world, pos, id); !problem.empty()) throw std::runtime_error(problem);

    ::donation_box &box = donation_at(*world, pos);
    box.gifts.push_back(::gift{box.next_id++, id, static_cast<short>(count), pPeer->user_id, pPeer->growid, clean(hPipe["note"], 128)});
    modify_item_inventory(event, ::slot(id, static_cast<short>(-count)));
    play_sfx(event.peer, "audio/page_turn.wav");
    tell_world(*world, pPeer->netid, std::format("`7[```5[```w{} `wplaces `5{}`` `2{}`` `winto the {}`5]```7]``",
        pPeer->growid, count, id_to_item(id).raw_name, id_to_item(world->blocks[cord(pos.x_int(), pos.y_int())].fg).raw_name));
    set_full(event, *world, pos, true);
    pPeer->mysql_update_all();
    world->save();
}

/* ---------------- Display Shelf ---------------- */

void shelf_wrench(ENetEvent &event, ::world &world, const ::pos &pos)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const ::item &block_item = id_to_item(world.blocks[cord(pos.x_int(), pos.y_int())].fg);
    auto shelf = std::ranges::find(world.shelves, pos, &::shelf::pos);
    std::array<u_int, 4> items{};
    if (shelf != world.shelves.end()) items = shelf->items;
    const bool any = std::ranges::any_of(items, [](u_int id) { return id != 0; });

    ::create_dialog dialog = ::create_dialog().set_default_color("`o");
    if (world.can_edit(*pPeer, pos, false))
    {
        dialog.add_label_with_icon("big", std::format("`w{}``", block_item.raw_name), block_item.id)
              .add_spacer("small");
        for (int i = 0; i < 4; ++i)
        {
            if (items[i] == 0) dialog.add_item_picker(std::format("add_{}", i), "`4Display an item``", "Select any item to display");
            else dialog.add_button(std::format("remove_{}", i), std::format("`oItem {}: `2{} `9(Click to remove)``", i + 1, id_to_item(static_cast<short>(items[i])).raw_name));
        }
        if (any) dialog.add_button("take_all", "Take All Items");
        dialog.embed_data("tilex", pos.x_int())
              .embed_data("tiley", pos.y_int());
        send_varlist(event.peer, { "OnDialogRequest", dialog.end_dialog("shelf", "", "OK") });
        return;
    }
    dialog.add_label_with_icon("big", std::format("`wViewing {}``", block_item.raw_name), block_item.id)
          .add_spacer("small");
    if (!any) dialog.add_textbox("There are no items being displayed");
    for (const u_int id : items)
        if (id != 0) dialog.add_label_with_icon("small", std::format("`2{}``", id_to_item(static_cast<short>(id)).raw_name), static_cast<int>(id));
    send_varlist(event.peer, { "OnDialogRequest", dialog.end_dialog("shelf_view", "OK", "") });
}

void shelf_edit(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::world *world = peer_world(pPeer);
    ::pos pos{};
    if (world == nullptr || !read_tile(hPipe, *world, type::DISPLAY_SHELF, pos)) return;
    if (!world->can_edit(*pPeer, pos, false)) return;

    auto found = std::ranges::find(world->shelves, pos, &::shelf::pos);
    if (found == world->shelves.end()) found = world->shelves.emplace(world->shelves.end(), ::shelf{.pos = pos});
    ::shelf &shelf = *found;
    bool changed = false;

    for (int i = 0; i < 4; ++i) // @note an item picked for an empty spot
    {
        const short id = static_cast<short>(atoi(hPipe[std::format("add_{}", i)].c_str()));
        if (id <= 0 || shelf.items[i] != 0) continue;
        const ::item &item = id_to_item(id);
        if (item.id == 0 || (item.cat & CAT_UNTRADEABLE) || item.type == type::FIST || item.type == type::WRENCH) throw std::runtime_error("You can't display that.");
        if (pPeer->have_slot(::slot(id, 0)) <= 0) return;
        modify_item_inventory(event, ::slot(id, -1));
        shelf.items[i] = static_cast<u_int>(id);
        changed = true;
    }
    const std::string button = hPipe["buttonClicked"];
    for (int i = 0; i < 4; ++i)
    {
        if (button != "take_all" && button != std::format("remove_{}", i)) continue;
        if (shelf.items[i] == 0) continue;
        const short id = static_cast<short>(shelf.items[i]);
        if (!pPeer->can_hold(id, 1))
        {
            on::ConsoleMessage(event.peer, std::format("`4You don't have room for`` `w{}``.", id_to_item(id).raw_name));
            continue;
        }
        modify_item_inventory(event, ::slot(id, 1));
        shelf.items[i] = 0;
        changed = true;
    }
    if (!changed) return;
    update_tile(event, *world, pos);
    play_in_world(world->name, pPeer->netid, "audio/blorb.wav");
    pPeer->mysql_update_all();
    world->save();
}
