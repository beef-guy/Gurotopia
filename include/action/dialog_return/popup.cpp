#include "pch.hpp"

#include "popup.hpp"
#include "action/trade.hpp"
#include "friends.hpp"
#include "daily_quest.hpp"
#include "action/profile.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "tools/create_dialog.hpp"
#include "action/carnival.hpp"
#include "action/transform.hpp"
#include <algorithm>
#include <unordered_map>

namespace
{
    /* @brief the other player in this world from the wrench menu's netID, or nullptr */
    ENetPeer *wrenched(ENetEvent &event, const ::hPipe &hPipe)
    {
        ::peer *pPeer = static_cast<::peer*>(event.peer->data);
        const int netid = atoi(hPipe["netID"].c_str());
        ENetPeer *other = nullptr;
        peers(pPeer->recent_worlds.back(), PeerSearch::WORLD, [&](ENetPeer *p)
        {
            const ::peer *pOthers = static_cast<::peer*>(p->data);
            if (other == nullptr && pOthers->netid == netid && p != event.peer) other = p;
        });
        return other;
    }
}

static const std::unordered_map<std::string, std::string> rml_pages{
    {"seed_diary_customization", "show_seed_diary_ui"},
    {"open_worldlock_storage", "show_world_lock_storage"}
};

void popup(ENetEvent& event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    if (transform_button_clicked(event, hPipe["buttonClicked"])) return; // @note the Transform buttons of the self wrench menu

    if (hPipe["buttonClicked"] == "my_worlds")
    {
        auto section = [](const auto &range) 
        {
            std::string result;
            for (const std::string &name : range)
                if (!name.empty())
                    result.append(std::format("add_button|{0}|{0}|noflags|0|0|\n", name));
            return result;
        };
        send_varlist(event.peer, {
            "OnDialogRequest",
            std::format(
                "set_default_color|`o\n"
                "start_custom_tabs|\n"
                "add_custom_button|myWorldsUiTab_0|image:interface/large/btn_tabs2.rttex;image_size:228,92;frame:1,0;width:0.15;|\n"
                "add_custom_button|myWorldsUiTab_1|image:interface/large/btn_tabs2.rttex;image_size:228,92;frame:0,1;width:0.15;|\n"
                "add_custom_button|myWorldsUiTab_2|image:interface/large/btn_tabs2.rttex;image_size:228,92;frame:0,2;width:0.15;|\n"
                "end_custom_tabs|\n"
                "add_label|big|Locked Worlds|left|0|\n"
                "add_spacer|small|\n"
                "add_textbox|Place a World Lock in a world to lock it. Break your World Lock to unlock a world.|left|\n"
                "add_spacer|small|\n"
                "{}\n"
                "add_spacer|small|\n"
                "end_dialog|worlds_list||Back|\n"
                "add_quick_exit|\n",
                section(pPeer->my_worlds)
            )
        });
    }
    else if (hPipe["buttonClicked"] == "billboard_edit")
    {
        const ::item &item = id_to_item(pPeer->billboard.id);

        send_varlist(event.peer, {
            "OnDialogRequest",
            std::format(
                "set_default_color|`o\n"
                "add_label_with_icon|big|`wTrade Billboard``|left|8282|\n"
                "add_spacer|small|\n"
                "{}"
                "add_item_picker|billboard_item|`wSelect Billboard Item``|Choose an item to put on your billboard!|\n"
                "add_spacer|small|\n"
                "add_checkbox|billboard_toggle|`$Show Billboard``|{}\n"
                "add_checkbox|billboard_buying_toggle|`$Is Buying``|{}\n"
                "add_text_input|setprice|Price of item:|{}|5|\n"
                "add_checkbox|chk_peritem|World Locks per Item|{}\n"
                "add_checkbox|chk_perlock|Items per World Lock|{}\n"
                "add_spacer|small|\n"
                "end_dialog|billboard_edit|Close|Update|\n",
                (pPeer->billboard.id == 0) ? 
                    "" : 
                    std::format("add_label_with_icon|small|`w{}``|left|{}|\n", item.raw_name, pPeer->billboard.id),
                to_char(pPeer->billboard.show),
                to_char(pPeer->billboard.isBuying),
                pPeer->billboard.price,
                to_char(pPeer->billboard.perItem),
                to_char(!pPeer->billboard.perItem)
            )
        });
    }
    else if (profile_button(event, hPipe["buttonClicked"])) {} // @note Wardrobe, Daily Bonus, Notebook, Growmojis, Trades...
    else if (const auto it = rml_pages.find(hPipe["buttonClicked"]); it != rml_pages.end())
    {
        send_varlist(event.peer, { "OnDialogRequestRML", it->second });
    }
    else if (hPipe["buttonClicked"] == "trade")
    {
        const int netid = atoi(hPipe["netID"].c_str()); // @note the wrench menu embeds the other player's netID

        ENetPeer *other = nullptr;
        peers(pPeer->recent_worlds.back(), PeerSearch::WORLD, [&](ENetPeer *p)
        {
            const ::peer *pOthers = static_cast<::peer*>(p->data);
            if (other == nullptr && pOthers->netid == netid && p != event.peer) other = p;
        });
        if (other != nullptr) trade::request(event, other);
    }
    else if (hPipe["buttonClicked"] == "goals") // @note the "Goals & Quests" button in the wrench menu
    {
        const bool jim_done = daily_quest_done(*pPeer);
        send_varlist(event.peer, { "OnDialogRequest", ::create_dialog()
            .set_default_color("`o")
            .add_label_with_icon("big", "`wGoals & Quests``", 1900)
            .add_spacer("small")
            .add_label_with_icon("small", "`wCrazy Jim's Daily Quest``", 3898)
            .add_smalltext(jim_done ? "`2Done for today!`` A new one comes at midnight." : "Bring Crazy Jim what he wants today for a Growtoken.")
            .add_button("goal_daily", "View Daily Quest")
            .add_spacer("small")
            .add_label_with_icon("small", "`9Quest For The Ring``", 1900)
            .add_smalltext(carnival_goal_line(*pPeer))
            .add_button("goal_ring", "View Quest For The Ring")
            .add_spacer("small")
            .add_quick_exit()
            .end_dialog("popup", "Close", "") });
    }
    else if (hPipe["buttonClicked"] == "goal_daily") show_daily_quest(event);
    else if (hPipe["buttonClicked"] == "goal_ring") carnival_goal_dialog(event.peer);
    else if (hPipe["buttonClicked"] == "show_clothes") // @note View worn clothes
    {
        ENetPeer *other = wrenched(event, hPipe);
        if (other == nullptr) return;
        const ::peer *pOthers = static_cast<::peer*>(other->data);
        ::create_dialog dialog = ::create_dialog()
            .set_default_color("`o")
            .add_label_with_icon("big", std::format("`w{}``'s clothes", pOthers->display_growid), 1784)
            .add_spacer("small");
        int worn = 0;
        for (const float id : pOthers->clothing)
        {
            const short item = static_cast<short>(id);
            if (item <= 0) continue;
            dialog.add_label_with_icon("small", std::format("`w{}``", id_to_item(item).raw_name), item);
            ++worn;
        }
        if (worn == 0) dialog.add_textbox("They aren't wearing anything special.");
        dialog.add_spacer("small").add_quick_exit();
        send_varlist(event.peer, { "OnDialogRequest", dialog.end_dialog("view_clothes", "", "Close") });
    }
    else if (hPipe["buttonClicked"] == "ignore_player")
    {
        ENetPeer *other = wrenched(event, hPipe);
        if (other == nullptr) return;
        const ::peer *pOthers = static_cast<::peer*>(other->data);
        if (auto it = std::ranges::find(pPeer->ignored, pOthers->user_id); it != pPeer->ignored.end())
        {
            pPeer->ignored.erase(it);
            on::ConsoleMessage(event.peer, std::format("`oYou are no longer ignoring {}``.", pOthers->display_growid));
        }
        else
        {
            pPeer->ignored.push_back(pOthers->user_id);
            on::ConsoleMessage(event.peer, std::format("`oYou are now ignoring {}``. You won't see their chat or messages until you log off. Wrench them and pick Ignore Player again to stop.", pOthers->display_growid));
        }
    }
    else if (hPipe["buttonClicked"] == "report_player")
    {
        ENetPeer *other = wrenched(event, hPipe);
        if (other == nullptr) return;
        const ::peer *pOthers = static_cast<::peer*>(other->data);
        send_varlist(event.peer, {
            "OnDialogRequest",
            ::create_dialog()
                .set_default_color("`o")
                .add_label_with_icon("big", std::format("`wReport {}``", pOthers->display_growid), 18)
                .add_spacer("small")
                .add_textbox("Tell the moderators what this player did. False reports may get you in trouble!")
                .add_text_input("reason", "Reason:", "", 100)
                .embed_data("netID", pOthers->netid)
                .end_dialog("report_player", "Cancel", "Report")
        });
    }
    else if (hPipe["buttonClicked"] == "friend_add")
    {
        friend_add(event, atoi(hPipe["netID"].c_str())); // @note the wrench menu embeds the other player's netID
    }
}
void report_player(ENetEvent& event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ENetPeer *other = wrenched(event, hPipe);
    std::string reason = hPipe["reason"];
    if (other == nullptr || reason.empty()) return;
    if (reason.size() > 100) reason.resize(100);
    std::erase_if(reason, [](char c) { return c == '|' || c == '\n' || c == '`'; });
    const ::peer *pOthers = static_cast<::peer*>(other->data);
    const std::string note = std::format("`4[REPORT]`` `w{}`` reported `w{}`` in `w{}``: {}", pPeer->growid, pOthers->growid, pPeer->recent_worlds.back(), reason);
    print(std::format("report: {} reported {} in {}: {}", pPeer->growid, pOthers->growid, pPeer->recent_worlds.back(), reason));
    peers("", PeerSearch::EVERYONE, [&](ENetPeer *p) // @note every moderator online sees it
    {
        const ::peer *pStaff = static_cast<::peer*>(p->data);
        if (pStaff != nullptr && pStaff->role >= MODERATOR) on::ConsoleMessage(p, note);
    });
    on::ConsoleMessage(event.peer, "`2Thanks for your report! The moderators will look into it.``");
}
