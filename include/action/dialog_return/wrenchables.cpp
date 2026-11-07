#include "pch.hpp"

#include "onVariant/ConsoleMessage.hpp"
#include "tools/create_dialog.hpp"
#include "tools/sound.hpp"
#include "daily_quest.hpp"
#include "commands/weather.hpp"
#include "action/ghosts.hpp"
#include "database/machines.hpp"

#include "wrenchables.hpp"

#include <algorithm>
#include <map>

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

    /* @note 52368: the B.O.O. office clears every ghost in the world for 15 World Locks */
    void boo_office(ENetEvent &event, const ::pos &pos)
    {
        ::peer *pPeer = static_cast<::peer*>(event.peer->data);
        ::world *world = peer_world(pPeer);
        const int ghosts = (world != nullptr) ? static_cast<int>(world->ghosts.size()) : 0;
        ::create_dialog dialog = ::create_dialog()
            .set_default_color("`o")
            .add_label_with_icon("big", "`wB.O.O. Headquarters``", NEUTRON_GUN)
            .add_textbox(ghosts == 0 ?
                "Battlers Of the Otherworldly here! Our instruments say there are no ghosts in your world. Call us when it gets spooky!" :
                std::format("Battlers Of the Otherworldly here! Our instruments say there {} `w{}`` ghost{} in your world. We'll clear them all out for `215 World Locks``.",
                    (ghosts == 1) ? "is" : "are", ghosts, (ghosts == 1) ? "" : "s"))
            .add_spacer("small");
        if (ghosts > 0) dialog.add_button("boo_clear", "Clear the ghosts (15 World Locks)");
        send_phone(event, pos, dialog);
    }

    void dial(ENetEvent &event, const ::pos &pos, const std::string &number)
    {
        if (number == "52368") { boo_office(event, pos); return; }
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
    else if (button == "boo_clear")
    {
        if (world->ghosts.empty()) return boo_office(event, pos);
        if (pPeer->have_slot(::slot(242/*World Lock*/, 0)) < 15) throw std::runtime_error("`4You need 15 World Locks for that.``");
        modify_item_inventory(event, ::slot(242, -15));
        const int gone = ghosts_clear(*world);
        play_in_world(world->name, pPeer->netid, "audio/boo_ghost_be_gone.wav");
        peers(world->name, PeerSearch::WORLD, [&](ENetPeer *peer) {
            on::ConsoleMessage(peer, std::format("`2The B.O.O. cleared this world!`` {} ghost{} gone.", gone, (gone == 1) ? "" : "s"));
        });
        pPeer->mysql_update_all();
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
        sales_bgl(event, pos); // @note straight back, so another 100 can be converted with one more tap
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

void donation_offer(ENetEvent &event, ::world &world, const ::pos &pos, short id)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (pPeer->have_slot(::slot(id, 0)) <= 0) return;
    if (const std::string problem = gift_problem(*pPeer, world, pos, id); !problem.empty()) throw std::runtime_error(problem);
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
}

void donation_edit(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::world *world = peer_world(pPeer);
    ::pos pos{};
    if (world == nullptr || !read_tile(hPipe, *world, type::DONATION_BOX, pos)) return;

    if (const short id = static_cast<short>(atoi(hPipe["donate_item"].c_str())); id > 0)
    {
        donation_offer(event, *world, pos, id);
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

/* ---------------- Heart Monitor ---------------- */

void heart_monitors_refresh(int uid)
{
    for (::world &world : worlds)
        for (const ::heart_monitor &heart : world.heart_monitors)
            if (heart.uid == uid) world_tile_update(world, heart.pos);
}

/* ---------------- Mannequin ---------------- */

namespace
{
    constexpr std::array<const char*, 9> slot_names{ "Hair", "Shirt", "Pants", "Shoes", "Face Item", "Hand Item", "Back Item", "Hat", "Necklace" };

    ::mannequin &mannequin_at(::world &world, const ::pos &pos)
    {
        auto dummy = std::ranges::find(world.mannequins, pos, &::mannequin::pos);
        if (dummy == world.mannequins.end()) dummy = world.mannequins.emplace(world.mannequins.end(), ::mannequin{.pos = pos});
        return *dummy;
    }

    /* @brief give a worn item back. @return false if it doesn't fit */
    bool take_off(ENetEvent &event, ::mannequin &dummy, int slot)
    {
        ::peer *pPeer = static_cast<::peer*>(event.peer->data);
        const short id = dummy.clothes[slot];
        if (id == 0) return true;
        if (!pPeer->can_hold(id, 1))
        {
            on::ConsoleMessage(event.peer, std::format("`4You don't have room for`` `w{}``.", id_to_item(id).raw_name));
            return false;
        }
        modify_item_inventory(event, ::slot(id, 1));
        dummy.clothes[slot] = 0;
        return true;
    }
}

void mannequin_offer(ENetEvent &event, ::world &world, const ::pos &pos, short id)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const ::item &item = id_to_item(id);
    if (!world.can_edit(*pPeer, pos, false)) { play_sfx(event.peer, "audio/cant_place_tile.wav"); return; }
    if (item.type != type::CLOTHING || item.cloth_type >= 9) return;
    if (item.cat & CAT_UNTRADEABLE || id == 5712/*Will of the Wild*/ || id == 10044/*Golem's Gift*/) throw std::runtime_error("That's too special to put on a mannequin!");
    auto dummy = std::ranges::find(world.mannequins, pos, &::mannequin::pos);
    if (dummy != world.mannequins.end() && dummy->clothes[item.cloth_type] == id) throw std::runtime_error("`2You gigle as you swap two identical items!");

    ::create_dialog dialog = ::create_dialog()
        .set_default_color("`o")
        .add_label_with_icon("big", std::format("`w{}``", item.raw_name), item.id)
        .add_spacer("small")
        .add_textbox(std::format("Do you really want to put your {} on the Mannequin?", item.raw_name))
        .add_spacer("small");
    dialog.embed_data("itemid", id)
          .embed_data("tilex", pos.x_int())
          .embed_data("tiley", pos.y_int());
    send_varlist(event.peer, { "OnDialogRequest", dialog.end_dialog("mannequin_dress", "No", "Yes") });
}

void mannequin_dress(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::world *world = peer_world(pPeer);
    ::pos pos{};
    if (world == nullptr || !read_tile(hPipe, *world, type::MANNEQUIN, pos)) return;
    if (!world->can_edit(*pPeer, pos, false)) return;
    const short id = static_cast<short>(atoi(hPipe["itemid"].c_str()));
    const ::item &item = id_to_item(id);
    if (id <= 0 || item.type != type::CLOTHING || item.cloth_type >= 9 || (item.cat & CAT_UNTRADEABLE) || id == 5712 || id == 10044) return;
    if (pPeer->have_slot(::slot(id, 0)) <= 0) return;

    ::mannequin &dummy = mannequin_at(*world, pos);
    if (dummy.clothes[item.cloth_type] == id) return;
    if (!take_off(event, dummy, item.cloth_type)) return; // @note what it wore there comes back to you
    modify_item_inventory(event, ::slot(id, -1));
    dummy.clothes[item.cloth_type] = id;
    update_tile(event, *world, pos);
    play_in_world(world->name, pPeer->netid, "audio/change_clothes.wav");
    pPeer->mysql_update_all();
    world->save();
}

void mannequin_wrench(ENetEvent &event, ::world &world, const ::pos &pos)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (!world.can_edit(*pPeer, pos, false)) { play_sfx(event.peer, "audio/cant_place_tile.wav"); return; }
    const ::item &block_item = id_to_item(world.blocks[cord(pos.x_int(), pos.y_int())].fg);
    auto dummy = std::ranges::find(world.mannequins, pos, &::mannequin::pos);

    ::create_dialog dialog = ::create_dialog()
        .set_default_color("`o")
        .add_label_with_icon("big", std::format("`wEdit {}``", block_item.raw_name), block_item.id)
        .add_spacer("small")
        .add_textbox("What would you like to write on its sign?")
        .add_text_input("label", "", (dummy != world.mannequins.end()) ? dummy->label : "", 128)
        .add_spacer("small");
    bool wearing = false;
    if (dummy != world.mannequins.end())
        for (int i = 0; i < 9; ++i)
        {
            if (dummy->clothes[i] == 0) continue;
            wearing = true;
            dialog.add_button(std::format("take_{}", i), std::format("`o{}: `2{} `9(Click to remove)``", slot_names[i], id_to_item(dummy->clothes[i]).raw_name));
        }
    if (wearing) dialog.add_button("take_all", "Take off all clothes");
    else dialog.add_smalltext("To dress it, use clothing from your backpack on the mannequin.");
    dialog.embed_data("tilex", pos.x_int())
          .embed_data("tiley", pos.y_int());
    send_varlist(event.peer, { "OnDialogRequest", dialog.end_dialog("mannequin", "Cancel", "OK") });
}

void mannequin_edit(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::world *world = peer_world(pPeer);
    ::pos pos{};
    if (world == nullptr || !read_tile(hPipe, *world, type::MANNEQUIN, pos)) return;
    if (!world->can_edit(*pPeer, pos, false)) return;

    ::mannequin &dummy = mannequin_at(*world, pos);
    const std::string button = hPipe["buttonClicked"];
    bool undressed = false;
    for (int i = 0; i < 9; ++i)
        if (button == "take_all" || button == std::format("take_{}", i))
            if (dummy.clothes[i] != 0 && take_off(event, dummy, i)) undressed = true;
    if (button.empty() || button == "OK") dummy.label = clean(hPipe["label"], 128);
    update_tile(event, *world, pos);
    if (undressed) play_in_world(world->name, pPeer->netid, "audio/change_clothes.wav");
    pPeer->mysql_update_all();
    world->save();
}

/* ---------------- Dice ---------------- */

void dice_wrench(ENetEvent &event, ::world &world, const ::pos &pos)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (!world.can_edit(*pPeer, pos, false)) { play_sfx(event.peer, "audio/cant_place_tile.wav"); return; }
    const ::block &block = world.blocks[cord(pos.x_int(), pos.y_int())];
    const ::item &item = id_to_item(block.fg);
    ::create_dialog dialog = ::create_dialog()
        .set_default_color("`o")
        .add_label_with_icon("big", std::format("`wEdit {}``", item.raw_name), item.id)
        .add_spacer("small")
        .add_checkbox("is_public", "Usable by public", block.state[2] & S_PUBLIC)
        .add_checkbox("is_silenced", "Silenced", block.state[3] & S_SILENCED);
    dialog.embed_data("tilex", pos.x_int())
          .embed_data("tiley", pos.y_int());
    send_varlist(event.peer, { "OnDialogRequest", dialog.end_dialog("dice", "Cancel", "OK") });
}

void dice_edit(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::world *world = peer_world(pPeer);
    ::pos pos{};
    if (world == nullptr || !read_tile(hPipe, *world, type::RANDOM, pos)) return;
    if (!world->can_edit(*pPeer, pos, false)) return;
    ::block &block = world->blocks[cord(pos.x_int(), pos.y_int())];
    if (hPipe["is_public"] == "1") block.state[2] |= S_PUBLIC; else block.state[2] &= ~S_PUBLIC;
    if (hPipe["is_silenced"] == "1") block.state[3] |= S_SILENCED; else block.state[3] &= ~S_SILENCED;
    update_tile(event, *world, pos);
    world->save();
}

/* ---------------- Weather Machines with settings ---------------- */

void weather_wrench(ENetEvent &event, ::world &world, const ::pos &pos)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (!world.can_edit(*pPeer, pos, false)) { play_sfx(event.peer, "audio/cant_place_tile.wav"); return; }
    const ::item &machine = id_to_item(world.blocks[cord(pos.x_int(), pos.y_int())].fg);
    auto w = std::ranges::find(world.weather_settings, pos, &::weather_setting::pos);

    ::create_dialog dialog = ::create_dialog()
        .set_default_color("`o")
        .add_label_with_icon("big", std::format("`w{}``", machine.raw_name), machine.id)
        .add_spacer("small");
    if (machine.id == 5000/*Background*/)
    {
        const std::string current = (w != world.weather_settings.end()) ? id_to_item(static_cast<short>(w->item)).raw_name : "None";
        dialog.add_textbox("`oYou can scan any Background Block to set it up in your weather machine.")
              .add_spacer("small")
              .add_item_picker("weather_item", std::format("`oItem: `2{}``", current), "Select any Background Block")
              .add_spacer("small");
    }
    else if (machine.id == 3832/*Stuff*/)
    {
        const std::string current = id_to_item(static_cast<short>((w != world.weather_settings.end()) ? w->item : 2)).raw_name;
        dialog.add_item_picker("weather_item", std::format("`oItem: `2{}``", current), "Select any item to rain down")
              .add_text_input("gravity", "`oGravity:", std::to_string((w != world.weather_settings.end()) ? w->gravity : 50), 6);
    }
    else return; // @note the other machines have nothing to set
    dialog.embed_data("tilex", pos.x_int())
          .embed_data("tiley", pos.y_int());
    send_varlist(event.peer, { "OnDialogRequest", dialog.end_dialog("weather", "Cancel", "Okay") });
}

void weather_edit(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::world *world = peer_world(pPeer);
    if (world == nullptr) return;
    const ::pos pos{atoi(hPipe["tilex"].c_str()), atoi(hPipe["tiley"].c_str())};
    if (!valid_tile(*world, pos) || !world->can_edit(*pPeer, pos, false)) return;
    const short machine = world->blocks[cord(pos.x_int(), pos.y_int())].fg;
    if (machine != 5000 && machine != 3832) return;

    auto w = std::ranges::find(world->weather_settings, pos, &::weather_setting::pos);
    if (w == world->weather_settings.end()) w = world->weather_settings.emplace(world->weather_settings.end(), ::weather_setting{.pos = pos, .item = (machine == 3832) ? 2u : 0u});

    if (const short id = static_cast<short>(atoi(hPipe["weather_item"].c_str())); id > 0)
    {
        const ::item &item = id_to_item(id);
        if (item.id == 0) return;
        if (machine == 5000 && item.type != type::BACKGROUND) throw std::runtime_error("That's not a Background Block!");
        w->item = static_cast<u_int>(id);
    }
    if (machine == 3832 && !hPipe["gravity"].empty()) w->gravity = std::clamp(atoi(hPipe["gravity"].c_str()), -500, 500);

    update_tile(event, *world, pos);
    if (world->weather == pos && (world->blocks[cord(pos.x_int(), pos.y_int())].state[2] & S_TOGGLE)) // @note running now: show the change straight away
    {
        peers(world->name, PeerSearch::WORLD, [machine](ENetPeer *peer)
        {
            send_varlist(peer, { "OnSetCurrentWeather", get_weather_id(machine) });
        });
    }
    world->save();
    if (!hPipe["weather_item"].empty()) weather_wrench(event, *world, pos); // @note an item was picked: show what it's set to now
}

/* ---------------- MAGPLANT 5000, Gaia's Beacon, Unstable Tesseract ---------------- */
/* @note texts and rules from github.com/cppdecl/Nomikai (BlockWrenchListener.hh, DialogCloseListener.hh "Magplant") */

namespace
{
    constexpr short ID_MAGPLANT_REMOTE = 5640;

    ::magplant &magplant_at(::world &world, const ::pos &pos)
    {
        auto mag = std::ranges::find(world.magplants, pos, &::magplant::pos);
        if (mag == world.magplants.end()) mag = world.magplants.emplace(world.magplants.end(), ::magplant{.pos = pos});
        return *mag;
    }

    void send_magplant(ENetEvent &event, const ::pos &pos, ::create_dialog &dialog, const std::string &close, const std::string &ok)
    {
        dialog.embed_data("tilex", pos.x_int())
              .embed_data("tiley", pos.y_int());
        send_varlist(event.peer, { "OnDialogRequest", dialog.end_dialog("magplant", close, ok) });
    }

    /* @return why this machine can't store the item, or "" */
    std::string magplant_rejects(short machine, const ::item &item)
    {
        if (item.id == 0 || (item.cat & CAT_UNTRADEABLE) || item.cloth_type != clothing::NONE || item.type == type::CLOTHING ||
            item.type == type::AURA || item.type == type::GEM || item.type == type::FIST || item.type == type::WRENCH ||
            item.id == ID_MAGPLANT_REMOTE)
            return "This item is not compatible with this machine.";
        const machines::accepts takes = machines::takes(machine); // @note Unstable Tesseract: no seeds, Gaia's Beacon: seeds only (the website can change it)
        if (takes == machines::NO_SEEDS && item.type == type::SEED) return "This item is not compatible with this machine.";
        if (takes == machines::SEEDS_ONLY && item.type != type::SEED) return "This item is not compatible with this machine.";
        return "";
    }
}

void magplant_give_remote(ENetEvent &event, ::world &world, const ::pos &pos)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    auto mag = std::ranges::find(world.magplants, pos, &::magplant::pos);
    if (mag == world.magplants.end() || mag->item == 0) return;
    if (const int old = pPeer->have_slot(::slot(ID_MAGPLANT_REMOTE, 0)); old > 0) modify_item_inventory(event, ::slot(ID_MAGPLANT_REMOTE, static_cast<short>(-old)));
    if (!pPeer->can_hold(ID_MAGPLANT_REMOTE, 1)) throw std::runtime_error("You don't have room in your backpack!");
    modify_item_inventory(event, ::slot(ID_MAGPLANT_REMOTE, 1));
    pPeer->magplant = pos;
    pPeer->magplant_world = world.name;
    send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, "`wYou received a `2Magplant `wRemote!", 0u, 1u });
    send_varlist(event.peer, { "OnPlanterActivated", static_cast<u_int>(mag->item), static_cast<u_int>(pos.x_int()), static_cast<u_int>(pos.y_int()) }, pPeer->netid); // @note the game reads three uints
    pPeer->mysql_update_all();
}

void magplant_take_remote(ENetEvent &event)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (pPeer == nullptr) return;
    pPeer->magplant = ::pos{-1, -1};
    pPeer->magplant_world.clear();
    if (const int have = pPeer->have_slot(::slot(ID_MAGPLANT_REMOTE, 0)); have > 0)
        modify_item_inventory(event, ::slot(ID_MAGPLANT_REMOTE, static_cast<short>(-have)));
}

void magplant_remotes_off(::world &world, const ::pos &pos)
{
    peers(world.name, PeerSearch::WORLD, [&](ENetPeer *peer)
    {
        ::peer *pPeer = static_cast<::peer*>(peer->data);
        if (pPeer->magplant_world != world.name || pPeer->magplant != pos) return;
        ENetEvent fake{.peer = peer};
        magplant_take_remote(fake);
    });
}

void magplant_wrench(ENetEvent &event, ::world &world, const ::pos &pos)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const ::item &machine = id_to_item(world.blocks[cord(pos.x_int(), pos.y_int())].fg);
    auto found = std::ranges::find(world.magplants, pos, &::magplant::pos);
    const ::magplant mag = (found != world.magplants.end()) ? *found : ::magplant{.pos = pos};
    const bool owner = world.can_edit(*pPeer, pos, false);
    const bool linked = pPeer->magplant == pos && pPeer->magplant_world == world.name;

    ::create_dialog dialog = ::create_dialog()
        .set_default_color("`o")
        .add_label_with_icon("big", std::format("`w{}``", machine.raw_name), machine.id)
        .add_spacer("small");

    if (mag.item == 0)
    {
        dialog.add_label("small", "`6This machine is empty.");
        if (owner) dialog.add_item_picker("SelectItem", "`wChoose Item``", std::format("Choose an item to put in the {}!", machine.raw_name));
        send_magplant(event, pos, dialog, "Close", "");
        return;
    }
    const std::string name = id_to_item(mag.item).raw_name;
    dialog.add_label_with_icon("small", std::format("`2{}``", name), mag.item);
    dialog.add_label("small", (mag.count > 0) ? std::format("`6The machine contains {} `2{}``", mag.count, name) : "`6The machine is currently empty!``");
    dialog.add_smalltext(std::format("`oIt holds up to {} items.``", magplant_limit(machine.id))); // @note the website's Machine Capacities

    if (owner)
    {
        dialog.add_spacer("small");
        const int have = pPeer->have_slot(::slot(mag.item, 0));
        if (have > 0 && mag.count < magplant_limit(machine.id)) dialog.add_button("AddItem", "Add Items to the machine");
        if (200 - have > 0 && mag.count > 0) dialog.add_button("TakeItem", "Retrieve Items");
        if (mag.count < 1) dialog.add_item_picker("SelectItem", "`wChange Item``", std::format("Choose an item to put in the {}!", machine.raw_name));
    }
    if (machines::has_remote(machine.id))
    {
        dialog.add_label("small", std::format("Building Mode: {}", mag.remote ? "ACTIVE" : "`6DISABLED"));
        if (owner || (mag.remote && !linked))
            dialog.add_label("small", mag.remote ? 
                std::format("Use the MAGPLANT 5000 Remote to build `2{}`` directly from the {}'s storage.", name, machine.raw_name) : "Punch to activate building mode.");
        dialog.add_spacer("small");
        if (!linked && mag.remote) dialog.add_button("GetRemote", "Get Remote");
    }
    if (owner)
    {
        dialog.add_checkbox("EnableSucking", "Enable Collection", mag.collect);
        send_magplant(event, pos, dialog, "Close", "Update");
        return;
    }
    send_magplant(event, pos, dialog, "Close", "");
}

void magplant_edit(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::world *world = peer_world(pPeer);
    ::pos pos{};
    if (world == nullptr || !read_tile(hPipe, *world, type::MAGPLANT, pos)) return;
    const ::item &machine = id_to_item(world->blocks[cord(pos.x_int(), pos.y_int())].fg);
    ::magplant &mag = magplant_at(*world, pos);
    const bool owner = world->can_edit(*pPeer, pos, false);
    const std::string button = hPipe["buttonClicked"];
    bool changed = false;

    if (button == "GetRemote")
    {
        if (!machines::has_remote(machine.id) || mag.item == 0 || !mag.remote) return;
        magplant_give_remote(event, *world, pos);
        pPeer->mysql_update_all();
        return;
    }
    if (!owner) return;

    const std::string name = id_to_item(mag.item).raw_name;
    if (button == "AddItem" && mag.item != 0)
    {
        const int have = pPeer->have_slot(::slot(mag.item, 0));
        ::create_dialog dialog = ::create_dialog()
            .set_default_color("`o")
            .add_label_with_icon("big", std::format("`w{}``", machine.raw_name), machine.id)
            .add_smalltext(std::format("You have {} `2{}`` in your backpack.", have, name))
            .add_label("small", std::format("How many `2{}`` would you like to add?", name))
            .add_text_input("AddAmount", "Amount:", std::to_string(have), 3);
        send_magplant(event, pos, dialog, "Close", "Confirm");
        return;
    }
    if (button == "TakeItem" && mag.item != 0)
    {
        ::create_dialog dialog = ::create_dialog()
            .set_default_color("`o")
            .add_label_with_icon("big", std::format("`w{}``", machine.raw_name), machine.id)
            .add_smalltext(std::format("The {} contains {} `2{}``", machine.raw_name, mag.count, name))
            .add_label("small", std::format("How many `2{}`` would you like to take?", name))
            .add_text_input("TakeAmount", "Amount:", std::to_string(std::min(mag.count, 200)), 3);
        send_magplant(event, pos, dialog, "Close", "Confirm");
        return;
    }
    if (!hPipe["AddAmount"].empty() && mag.item != 0)
    {
        int amount = std::min({atoi(hPipe["AddAmount"].c_str()), 200, static_cast<int>(pPeer->have_slot(::slot(mag.item, 0)))});
        if (amount < 1) return;
        const int space = magplant_limit(machine.id) - mag.count;
        if (amount > space)
        {
            send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, std::format("That won't fit into the `2{}``!", machine.raw_name), 0u, 1u });
            amount = space;
            if (amount < 1) return;
        }
        modify_item_inventory(event, ::slot(mag.item, static_cast<short>(-amount)));
        mag.count += amount;
        on::ConsoleMessage(event.peer, std::format("Added {} {} to the {}", amount, name, machine.raw_name));
        send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, std::format("Added {} {} to the `2{}``!", amount, name, machine.raw_name), 0u, 1u });
        changed = true;
    }
    else if (!hPipe["TakeAmount"].empty() && mag.item != 0)
    {
        const int amount = std::min({atoi(hPipe["TakeAmount"].c_str()), 200, mag.count});
        if (amount < 1) return;
        if (!pPeer->can_hold(mag.item, amount)) throw std::runtime_error("That won't fit into your inventory.");
        modify_item_inventory(event, ::slot(mag.item, static_cast<short>(amount)));
        mag.count -= amount;
        on::ConsoleMessage(event.peer, std::format("Took {} {} from the {}", amount, name, machine.raw_name));
        send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, std::format("Took {} {} from the `2{}``!", amount, name, machine.raw_name), 0u, 1u });
        changed = true;
    }
    if (const short id = static_cast<short>(atoi(hPipe["SelectItem"].c_str())); id > 0)
    {
        if (mag.count > 0) return; // @note empty it first
        const std::string problem = magplant_rejects(machine.id, id_to_item(id));
        if (!problem.empty()) throw std::runtime_error(problem);
        mag.item = id;
        mag.count = 0;
        changed = true;
    }
    if (const std::string collect = hPipe["EnableSucking"]; !collect.empty() && mag.item != 0)
    {
        const bool on = collect == "1";
        if (mag.collect != on) mag.collect = on, changed = true;
    }
    if (!changed) return;
    update_tile(event, *world, pos);
    pPeer->mysql_update_all();
    world->save();
}

/* ---------------- GrowScan 9000 ---------------- */

namespace
{
    /* @brief one small icon line per item with its count, most first. tapping one points the arrow at it */
    std::string count_list(std::map<short, int> counts, const std::string &prefix)
    {
        std::vector<std::pair<short, int>> sorted(counts.begin(), counts.end());
        std::ranges::sort(sorted, [](const auto &a, const auto &b) { return a.second != b.second ? a.second > b.second : a.first < b.first; });
        std::string rows{};
        std::size_t shown = 0;
        for (const auto &[id, count] : sorted)
        {
            if (++shown > 200) break; // @note keep the dialog small enough for the game
            if (prefix.empty()) rows += std::format("add_label_with_icon|small|`w{}``: {}|left|{}|\n", id_to_item(id).raw_name, count, id);
            else rows += std::format("add_label_with_icon_button|small|`w{}``: {}|left|{}|{}{}|\n", id_to_item(id).raw_name, count, id, prefix, id);
        }
        return rows;
    }

    void send_growscan(ENetEvent &event, const ::pos &pos, ::create_dialog &dialog, const std::string &close, const std::string &ok)
    {
        dialog.embed_data("tilex", pos.x_int())
              .embed_data("tiley", pos.y_int());
        send_varlist(event.peer, { "OnDialogRequest", dialog.end_dialog("growscan", close, ok) });
    }

    bool growscan_usable(const ::peer &peer, ::world &world, const ::pos &pos)
    {
        return world.can_edit(peer, pos, false) || (world.blocks[cord(pos.x_int(), pos.y_int())].state[2] & S_PUBLIC);
    }
}

void growscan_wrench(ENetEvent &event, ::world &world, const ::pos &pos)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (!growscan_usable(*pPeer, world, pos))
    {
        play_sfx(event.peer, "audio/cant_place_tile.wav");
        throw std::runtime_error("This GrowScan 9000 isn't usable by the public.");
    }
    const ::item &machine = id_to_item(world.blocks[cord(pos.x_int(), pos.y_int())].fg);
    const bool owner = world.can_edit(*pPeer, pos, false);

    ::create_dialog dialog = ::create_dialog()
        .set_default_color("`o")
        .add_label_with_icon("big", std::format("`w{}``", machine.raw_name), machine.id)
        .add_spacer("small")
        .add_textbox(std::format("`oThis amazing machine can scan `w{}`` for blocks or floating items! What would you like to see?``", world.name))
        .add_button("scan_blocks", "`wWorld Blocks``")
        .add_button("scan_floating", "`wFloating Items``");
    if (owner)
    {
        dialog.add_spacer("small")
              .add_checkbox("is_public", "`oUsable by public``", world.blocks[cord(pos.x_int(), pos.y_int())].state[2] & S_PUBLIC);
        send_growscan(event, pos, dialog, "Close", "OK");
        return;
    }
    send_growscan(event, pos, dialog, "Close", "");
}

void growscan_edit(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::world *world = peer_world(pPeer);
    ::pos pos{};
    if (world == nullptr || !read_tile(hPipe, *world, type::GROWSCAN, pos)) return;
    if (!growscan_usable(*pPeer, *world, pos)) return;
    const ::item &machine = id_to_item(world->blocks[cord(pos.x_int(), pos.y_int())].fg);
    const std::string button = hPipe["buttonClicked"];

    if (button == "scan_blocks")
    {
        std::map<short, int> counts{};
        int total = 0;
        for (const ::block &block : world->blocks)
        {
            if (block.fg != 0) ++counts[block.fg], ++total;
            if (block.bg != 0) ++counts[block.bg], ++total;
        }
        ::create_dialog dialog = ::create_dialog()
            .set_default_color("`o")
            .add_label_with_icon("big", "`wWorld Blocks``", machine.id)
            .add_spacer("small")
            .add_textbox(std::format("`w{}`` has `w{}`` blocks of `w{}`` kinds.", world->name, total, counts.size()))
            .add_spacer("small")
            .add_raw(count_list(std::move(counts), ""))
            .add_spacer("small")
            .add_button("back", "`wBack``");
        send_growscan(event, pos, dialog, "Close", "");
        return;
    }
    if (button == "scan_floating")
    {
        std::map<short, int> counts{};
        int total = 0;
        for (const ::object &object : world->objects)
        {
            counts[static_cast<short>(object.id)] += object.count;
            total += object.count;
        }
        ::create_dialog dialog = ::create_dialog()
            .set_default_color("`o")
            .add_label_with_icon("big", "`wFloating Items``", machine.id)
            .add_spacer("small");
        if (counts.empty()) dialog.add_textbox("`oThere are no floating items in this world.``");
        else dialog.add_textbox(std::format("`oThere are `w{}`` floating items (`w{}`` kinds) in `w{}``. Tap one to point at it.``", total, counts.size(), world->name))
                   .add_spacer("small")
                   .add_raw(count_list(std::move(counts), "findf_"));
        dialog.add_spacer("small")
              .add_button("back", "`wBack``");
        send_growscan(event, pos, dialog, "Close", "");
        return;
    }
    if (button.starts_with("findf_")) // @note a green arrow points at the item (PACKET_ACTIVE_ARROW_TO_ITEM)
    {
        const int id = atoi(button.c_str() + 6);
        if (id <= 0 || id_to_item(static_cast<short>(id)).id == 0) return;
        ::gamePacket arrow{.type = 0x25 | (1/*nearest dropped item with this id*/ << 8), .netid = id};
        send_data(*event.peer, compress_state(arrow));
        return;
    }
    if (button == "back")
    {
        growscan_wrench(event, *world, pos);
        return;
    }
    if (!world->can_edit(*pPeer, pos, false) || hPipe["is_public"].empty()) return;
    ::block &block = world->blocks[cord(pos.x_int(), pos.y_int())];
    const bool was = block.state[2] & S_PUBLIC;
    const bool now = hPipe["is_public"] == "1";
    if (was == now) return;
    if (now) block.state[2] |= S_PUBLIC;
    else block.state[2] &= ~S_PUBLIC;
    update_tile(event, *world, pos);
    world->save();
}
