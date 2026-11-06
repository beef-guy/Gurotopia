#include "pch.hpp"

#include "onVariant/ConsoleMessage.hpp"
#include "onVariant/SetClothing.hpp"
#include "onVariant/SetBux.hpp"
#include "commands/punch.hpp"
#include "database/playmods.hpp"
#include "tools/create_dialog.hpp"
#include "tools/sound.hpp"

#include <algorithm>
#include <unordered_map>

#include "profile.hpp"

extern std::unordered_map<std::string_view, std::string_view> emoticon; // @note onVariant/EmoticonDataChanged.cpp

namespace
{
    constexpr short GROWTOKEN = 1486;

    /* @note the Daily Bonus: a 7 day streak, then it starts over. {gems, item, count} */
    struct bonus_day { int gems; short item; short count; };
    constexpr bonus_day bonus_days[7] = {
        {250, 0, 0}, {500, 0, 0}, {750, 0, 0}, {1000, 0, 0}, {1500, 0, 0}, {2000, 0, 0}, {2500, GROWTOKEN, 1}
    };

    std::tm utc_at(std::time_t when)
    {
        std::tm utc{};
#ifdef _WIN32
        gmtime_s(&utc, &when);
#else
        gmtime_r(&when, &utc);
#endif
        return utc;
    }
    int day_of(std::time_t when) { const std::tm utc = utc_at(when); return (utc.tm_year + 1900) * 10000 + (utc.tm_mon + 1) * 100 + utc.tm_mday; }
    int today() { return day_of(std::time(nullptr)); }
    int yesterday() { return day_of(std::time(nullptr) - 86400); }

    std::string until_midnight()
    {
        const long long left = 86400ll - static_cast<long long>(std::time(nullptr) % 86400);
        return std::format("{}h {}m", left / 3600, (left % 3600) / 60);
    }

    /* @return the streak day (1-7) the next claim pays */
    int next_bonus_day(const ::peer &peer)
    {
        const int streak = (peer.daily_bonus == today() || peer.daily_bonus == yesterday()) ? peer.bonus_streak : 0;
        if (peer.daily_bonus == today()) return std::max(streak, 1);
        return (streak % 7) + 1;
    }

    /* -- wardrobe -- */

    using outfit = std::array<int, 10>;

    std::vector<outfit> outfits(const ::peer &peer)
    {
        std::vector<outfit> result(WARDROBE_MAX_SLOTS, outfit{});
        const std::vector<std::string> saved = readch(peer.wardrobe, ';');
        for (std::size_t i = 0; i < saved.size() && i < result.size(); ++i)
        {
            const std::vector<std::string> ids = readch(saved[i], ',');
            if (ids.size() != 10) continue;
            for (std::size_t s = 0; s < 10; ++s) result[i][s] = std::max(atoi(ids[s].c_str()), 0);
        }
        return result;
    }

    std::string outfit_text(const outfit &o)
    {
        std::string text{};
        for (std::size_t s = 0; s < o.size(); ++s) text += std::format("{}{}", (s == 0) ? "" : ",", o[s]);
        return text;
    }

    void save_outfits(::peer &peer, const std::vector<outfit> &list)
    {
        std::string text{};
        for (std::size_t i = 0; i < list.size(); ++i) text += std::format("{}{}", (i == 0) ? "" : ";", outfit_text(list[i]));
        peer.wardrobe = text;
        peer.mysql_update("wardrobe", text);
    }

    /* @note what the wardrobe's save/load screen shows */
    void send_presets(ENetEvent &event)
    {
        const ::peer *pPeer = static_cast<::peer*>(event.peer->data);
        std::string presets{};
        for (const outfit &o : outfits(*pPeer)) presets += std::format("{}\"{}\"", presets.empty() ? "" : ",", outfit_text(o));
        send_varlist(event.peer, {
            "OnWardrobePresetUpdate",
            std::format("{{\"PresetCount\":{},\"Presets\":[{}]}}", pPeer->wardrobe_slots, presets)
        });
    }

    /* @note after the clothes changed: powers, punch effect, everyone sees it */
    void clothes_changed(ENetEvent &event)
    {
        ::peer *pPeer = static_cast<::peer*>(event.peer->data);
        pPeer->update_effects();
        for (float id : pPeer->clothing)
            if (u_short punch_id = get_punch_id(static_cast<u_int>(id)); punch_id != 0) pPeer->punch_effect = punch_id;
        on::SetClothing(event.peer, true);
    }

    int preset_index(const std::string &header)
    {
        const std::string text = ::hPipe{ header }["preset"];
        if (text.empty() || !std::isdigit(static_cast<u_char>(text.front()))) return -1;
        return atoi(text.c_str());
    }

    /* -- pages -- */

    void show_online_status(ENetEvent &event)
    {
        const ::peer *pPeer = static_cast<::peer*>(event.peer->data);
        send_varlist(event.peer, {
            "OnDialogRequest",
            ::create_dialog()
                .set_default_color("`o")
                .add_label_with_icon("big", "`wOnline Status``", 1366)
                .add_spacer("small")
                .add_textbox("Choose what your friends see when they look you up.")
                .add_spacer("small")
                .add_checkbox("status_online", "`2Online``", pPeer->online_status == 0)
                .add_checkbox("status_busy", "`4Busy``", pPeer->online_status == 1)
                .add_checkbox("status_away", "`6Away``", pPeer->online_status == 2)
                .add_spacer("small")
                .end_dialog("online_status", "Cancel", "OK")
        });
    }

    void show_notebook(ENetEvent &event)
    {
        const ::peer *pPeer = static_cast<::peer*>(event.peer->data);
        send_varlist(event.peer, {
            "OnDialogRequest",
            ::create_dialog()
                .set_default_color("`o")
                .add_label_with_icon("big", "`wNotebook``", 6000)
                .add_spacer("small")
                .add_textbox("Write anything you want to remember. Only you can see it.")
                .add_raw(std::format("add_text_box_input|personal_note||{}|200|5|\n", pPeer->notebook))
                .add_spacer("small")
                .add_button("clear_note", "`4Clear``")
                .end_dialog("notebook_edit", "Cancel", "Save")
        });
    }

    void show_growmojis(ENetEvent &event)
    {
        std::vector<std::pair<std::string_view, std::string_view>> list(emoticon.begin(), emoticon.end());
        std::ranges::sort(list, {}, &std::pair<std::string_view, std::string_view>::second);

        std::string lines{};
        for (std::size_t i = 0; i < list.size(); i += 4)
        {
            std::string row{};
            for (std::size_t j = i; j < std::min(i + 4, list.size()); ++j)
                row += std::format("{} `w({})``     ", list[j].second, list[j].first);
            lines += std::format("add_smalltext|{}|left|\n", row);
        }
        send_varlist(event.peer, {
            "OnDialogRequest",
            ::create_dialog()
                .set_default_color("`o")
                .add_label("big", "`wGrowmojis``")
                .add_spacer("small")
                .add_textbox(std::format("You have unlocked `2{}/{}`` Growmojis. Type the word in brackets in chat to use one, like `w(wl)``.", list.size(), list.size()))
                .add_spacer("small")
                .add_raw(lines)
                .add_spacer("small")
                .add_quick_exit()
                .end_dialog("growmojis", "Close", "")
        });
    }

    std::string trade_side(const std::string &items)
    {
        std::string text{};
        for (const std::string &part : readch(items, ' '))
        {
            const std::size_t x = part.find('x');
            if (part.empty() || x == std::string::npos) continue;
            const int id = atoi(part.substr(0, x).c_str()), count = atoi(part.substr(x + 1).c_str());
            if (id <= 0 || count <= 0) continue;
            if (!text.empty()) text += ", ";
            text += std::format("{} {}", count, id_to_item(id).raw_name);
        }
        return text.empty() ? "nothing" : text;
    }

    void show_trades(ENetEvent &event)
    {
        const ::peer *pPeer = static_cast<::peer*>(event.peer->data);
        ::create_dialog dialog = ::create_dialog()
            .set_default_color("`o")
            .add_label_with_icon("big", "`wTrade History``", 242)
            .add_spacer("small");

        int shown = 0;
        {
            ::hStmt hStmt{ "SELECT DATE_FORMAT(at, '%Y-%m-%d %H:%i'), actor, target, detail FROM mod_log WHERE action = 'trade' AND (actor = ? OR target = ?) ORDER BY id DESC LIMIT 25" };
            MYSQL_BIND params[2] = { make_bind_in(pPeer->growid), make_bind_in(pPeer->growid) };
            hStmt.bind_param(params);

            std::string at{}, actor{}, target{}, detail{};
            u_long at_length{}, actor_length{}, target_length{}, detail_length{};
            MYSQL_BIND results[4] = { make_bind_out(at), make_bind_out(actor), make_bind_out(target), make_bind_out(detail) };
            results[0].length = &at_length;
            results[1].length = &actor_length;
            results[2].length = &target_length;
            results[3].length = &detail_length;
            if (hStmt.pStmt != nullptr && mysql_stmt_bind_result(hStmt.pStmt, results) == 0)
            {
                hStmt.execute();
                while (mysql_stmt_fetch(hStmt.pStmt) == 0)
                {
                    auto cut = [](const std::string &s, u_long n) { return std::string(s.data(), std::min<u_long>(n, s.size())); };
                    const std::string when = cut(at, at_length), a = cut(actor, actor_length), b = cut(target, target_length), d = cut(detail, detail_length);
                    const std::size_t split = d.find("for ");
                    const std::string a_gave = trade_side(d.substr(0, split)), b_gave = (split == std::string::npos) ? "nothing" : trade_side(d.substr(split + 4));
                    const bool me_first = (a == pPeer->growid);
                    dialog.add_smalltext(std::format("`w{}`` with `w{}``: you gave `4{}`` and got `2{}``",
                        when, me_first ? b : a, me_first ? a_gave : b_gave, me_first ? b_gave : a_gave));
                    ++shown;
                }
            }
        }
        if (shown == 0) dialog.add_textbox("You haven't traded with anyone yet.");
        dialog.add_spacer("small").add_quick_exit();
        send_varlist(event.peer, { "OnDialogRequest", dialog.end_dialog("trade_history", "Close", "") });
    }

    std::string bonus_dialog(const ::peer &peer)
    {
        const bool claimed = (peer.daily_bonus == today());
        const int next = next_bonus_day(peer);

        std::string text = std::format(
            "set_default_color|`o\n"
            "add_label_with_icon|big|`wDaily Bonus``|left|{}|\n"
            "add_spacer|small|\n"
            "add_textbox|Come back every day to claim a bonus. Claim 7 days in a row for a `2Growtoken``! Miss a day and the streak starts over.|left|\n"
            "add_spacer|small|\n", GROWTOKEN);
        for (int day = 1; day <= 7; ++day)
        {
            const bonus_day &b = bonus_days[day - 1];
            const bool done = claimed ? (day <= next) : (day < next);
            const std::string reward = (b.item != 0) ? std::format("{} Gems + {} {}", b.gems, b.count, id_to_item(b.item).raw_name) : std::format("{} Gems", b.gems);
            text += std::format("add_label_with_icon|small|{}Day {}: {}``|left|{}|\n",
                done ? "`2" : (day == next ? "`w" : "`o"), day, done ? reward + " (claimed)" : reward, (b.item != 0) ? b.item : 112/*gem*/);
        }
        text += "add_spacer|small|\n";
        if (claimed)
            text += std::format("add_countdown|Next bonus in {}|disable|\n", until_midnight());
        else
            text += std::format("add_countdown|Day {} is ready!|enable|\n"
                                "add_button|claimbutton|`2Claim Day {}``|noflags|0|0|\n", next, next);
        text += "add_quick_exit|\n"
                "end_dialog|daily_bonus|Close||\n";
        return text;
    }

    void show_bonus(ENetEvent &event)
    {
        send_varlist(event.peer, { "OnDailyRewardRequest", bonus_dialog(*static_cast<::peer*>(event.peer->data)) });
    }

    void claim_bonus(ENetEvent &event)
    {
        ::peer *pPeer = static_cast<::peer*>(event.peer->data);
        if (pPeer->daily_bonus == today())
        {
            on::ConsoleMessage(event.peer, std::format("You already claimed today's bonus. Come back in {}.", until_midnight()));
            return;
        }
        const int day = next_bonus_day(*pPeer);
        const bonus_day &b = bonus_days[day - 1];
        if (b.item != 0 && !pPeer->can_hold(b.item, b.count))
        {
            on::ConsoleMessage(event.peer, "`4Make room in your backpack first!``");
            return;
        }
        pPeer->daily_bonus = today();
        pPeer->bonus_streak = day;
        pPeer->mysql_update("daily_bonus", pPeer->daily_bonus);
        pPeer->mysql_update("bonus_streak", pPeer->bonus_streak);

        pPeer->gems += b.gems;
        on::SetBux(event);
        if (b.item != 0) modify_item_inventory(event, ::slot(b.item, b.count));
        pPeer->mysql_update_all();

        const std::string got = (b.item != 0) ? std::format("`2{}`` Gems and `2{} {}``", b.gems, b.count, id_to_item(b.item).raw_name) : std::format("`2{}`` Gems", b.gems);
        on::ConsoleMessage(event.peer, std::format("Daily Bonus day {}: you got {}!", day, got));
        send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, std::format("Daily Bonus: {}!", got), 0u, 1u });
        play_sfx(event.peer, "audio/piano_nice.wav");
    }

    void coming_soon(ENetEvent &event, const std::string &title, int icon, const std::string &text)
    {
        send_varlist(event.peer, {
            "OnDialogRequest",
            ::create_dialog()
                .set_default_color("`o")
                .add_label_with_icon("big", std::format("`w{}``", title), icon)
                .add_spacer("small")
                .add_textbox(text)
                .add_spacer("small")
                .add_quick_exit()
                .end_dialog("profile_info", "Close", "")
        });
    }

    void show_titles(ENetEvent &event)
    {
        const ::peer *pPeer = static_cast<::peer*>(event.peer->data);
        std::string owned{};
        if (pPeer->role >= DEVELOPER) owned += "add_label_with_icon|small|`6@Developer`` (shown in your name)|left|32|\n";
        else if (pPeer->role == MODERATOR) owned += "add_label_with_icon|small|`#@Moderator`` (shown in your name)|left|32|\n";

        ::create_dialog dialog = ::create_dialog()
            .set_default_color("`o")
            .add_label_with_icon("big", "`wTitles``", 1794)
            .add_spacer("small");
        if (owned.empty()) dialog.add_textbox("You haven't earned any titles yet. Titles come from achievements, which aren't on this server yet.");
        else dialog.add_textbox("Your titles:").add_raw(owned);
        dialog.add_spacer("small").add_quick_exit();
        send_varlist(event.peer, { "OnDialogRequest", dialog.end_dialog("profile_info", "Close", "") });
    }
}

std::string online_status_text(const ::peer &peer)
{
    switch (peer.online_status)
    {
        case 1: return " (`4Busy``)";
        case 2: return " (`6Away``)";
        default: return "";
    }
}

bool profile_button(ENetEvent& event, const std::string &button)
{
    if (button == "wardrobe_customization") send_varlist(event.peer, { "OnDialogRequestRML", "show_wardrobe_main_ui" });
    else if (button == "bonus") show_bonus(event);
    else if (button == "set_online_status") show_online_status(event);
    else if (button == "notebook_edit") show_notebook(event);
    else if (button == "emojis") show_growmojis(event);
    else if (button == "trade_scan") show_trades(event);
    else if (button == "title_edit") show_titles(event);
    else if (button == "alist") coming_soon(event, "Achievements", 1794, "Achievements aren't on this server yet. They're coming in a later update!");
    else if (button == "marvelous_missions") coming_soon(event, "Marvelous Missions", 1486, "Marvelous Missions aren't on this server yet. They're coming in a later update!");
    else if (button == "pets" || button == "renew_pvp_license") coming_soon(event, "Card Battles", 1486, "Battle Pets and Card Battles aren't on this server yet. They're coming in a later update!");
    else if (button == "open_personlize_profile") coming_soon(event, "Personalize Profile", 32, "Profile customization isn't on this server yet. It's coming in a later update!");
    else if (button == "wrench_customization") coming_soon(event, "Wrench Customization", 32, "Wrench menu customization isn't on this server yet. It's coming in a later update!");
    else return false;
    return true;
}

void online_status(ENetEvent& event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    int status = pPeer->online_status;
    /* @note checkboxes: take the one that changed to ticked, else keep */
    const bool online = hPipe["status_online"] == "1", busy = hPipe["status_busy"] == "1", away = hPipe["status_away"] == "1";
    if (busy && status != 1) status = 1;
    else if (away && status != 2) status = 2;
    else if (online && status != 0) status = 0;
    else if (!online && !busy && !away) status = 0;
    if (status == pPeer->online_status) return;

    pPeer->online_status = status;
    pPeer->mysql_update("online_status", status);
    static constexpr const char *names[] = { "`2Online``", "`4Busy``", "`6Away``" };
    on::ConsoleMessage(event.peer, std::format("Your status is now {}.", names[status]));
}

void notebook_edit(ENetEvent& event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    std::string note = (hPipe["buttonClicked"] == "clear_note") ? "" : hPipe["personal_note"];
    if (note.size() > 200) note.resize(200);
    std::erase_if(note, [](char c) { return c == '|' || c == '\n' || c == '\r'; });
    if (note == pPeer->notebook) return;

    pPeer->notebook = note;
    pPeer->mysql_update("notebook", note);
    on::ConsoleMessage(event.peer, note.empty() ? "Notebook cleared." : "Notebook saved.");
}

void daily_bonus(ENetEvent& event, const ::hPipe &hPipe)
{
    if (hPipe["buttonClicked"] == "claimbutton") claim_bonus(event);
}

void action::wardrobe_preset_load(ENetEvent& event, const std::string&)
{
    send_presets(event);
}

void action::wardrobe_save_ui(ENetEvent& event, const std::string& header)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const int index = preset_index(header);
    if (index < 0 || index >= pPeer->wardrobe_slots) return;

    std::vector<outfit> list = outfits(*pPeer);
    for (std::size_t s = 0; s < 10; ++s) list[index][s] = static_cast<int>(pPeer->clothing[s]);
    save_outfits(*pPeer, list);
    on::ConsoleMessage(event.peer, std::format("Outfit saved in wardrobe slot `w{}``.", index + 1));
    send_presets(event);
}

void action::wardrobe_load_ui(ENetEvent& event, const std::string& header)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const int index = preset_index(header);
    if (index < 0 || index >= pPeer->wardrobe_slots) return;

    const outfit o = outfits(*pPeer)[index];
    if (std::ranges::all_of(o, [](int id) { return id == 0; }))
    {
        on::ConsoleMessage(event.peer, "That wardrobe slot is empty. Save an outfit in it first!");
        return;
    }
    int missing = 0;
    for (std::size_t s = 0; s < 10; ++s)
    {
        const int id = o[s];
        if (id == 0) { pPeer->clothing[s] = 0; continue; }
        const ::item &item = id_to_item(id);
        if (item.cloth_type != s || pPeer->have_slot(::slot(id, 0)) == 0) { ++missing; continue; }
        pPeer->clothing[s] = static_cast<float>(id);
    }
    clothes_changed(event);
    on::ConsoleMessage(event.peer, (missing == 0) ?
        std::format("Outfit {} put on.", index + 1) :
        std::format("Outfit {} put on. `4{}`` item{} you don't have anymore were skipped.", index + 1, missing, missing == 1 ? "" : "s"));
}

void action::wardrobe_unequip_all(ENetEvent& event, const std::string&)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (std::ranges::all_of(pPeer->clothing, [](float id) { return id == 0; })) return;
    pPeer->clothing.fill(0);
    clothes_changed(event);
}

void action::wardrobe_purchase_slot(ENetEvent& event, const std::string&)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (pPeer->wardrobe_slots >= WARDROBE_MAX_SLOTS)
    {
        on::ConsoleMessage(event.peer, "Your wardrobe already has every slot!");
        return;
    }
    const long long price = 10'000ll * (pPeer->wardrobe_slots - 1); // @note 10k, 20k, 30k, 40k gems
    if (pPeer->gems < price)
    {
        on::ConsoleMessage(event.peer, std::format("`4You need {} Gems to unlock another wardrobe slot.``", price));
        return;
    }
    pPeer->gems -= price;
    ++pPeer->wardrobe_slots;
    on::SetBux(event);
    pPeer->mysql_update("wardrobe_slots", pPeer->wardrobe_slots);
    pPeer->mysql_update_all();
    on::ConsoleMessage(event.peer, std::format("You unlocked wardrobe slot `w{}`` for `2{}`` Gems!", pPeer->wardrobe_slots, price));
    send_presets(event);
}

void action::dailyrewardmenu(ENetEvent& event, const std::string&)
{
    show_bonus(event);
}

void action::claimdailyreward(ENetEvent& event, const std::string&)
{
    claim_bonus(event);
}
