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

#include "onVariant/EmoticonDataChanged.hpp"
#include "growpass.hpp"
#include "profile.hpp"

extern std::unordered_map<std::string_view, std::string_view> emoticon; // @note onVariant/EmoticonDataChanged.cpp

namespace
{
    /* @note Growmojis that start locked, and the Growmoji item that unlocks each one. every other Growmoji is free */
    struct growmoji { std::string_view code; short item; };
    constexpr growmoji growmoji_items[] = {
        {"wl", 5770}, {"grow", 5772}, {"gems", 5774}, {"gtoken", 5776}, {"vend", 5778}, {"oops", 5804}, {"sleep", 5806},
        {"punch", 5808}, {"bheart", 5810}, {"cry", 5812}, {"bunny", 6002}, {"cactus", 6052}, {"pine", 6122}, {"peace", 6272},
        {"terror", 6274}, {"troll", 6276}, {"evil", 6278}, {"fireworks", 6322}, {"football", 6672}, {"alien", 6744},
        {"party", 6822}, {"pizza", 6824}, {"clap", 6894}, {"song", 7062}, {"ghost", 7130}, {"halo", 7232}, {"nuke", 7234},
        {"turkey", 7370}, {"gift", 7474}, {"cake", 7656}, {"heartarrow", 7684}, {"lucky", 7698}, {"shamrock", 7932},
        {"grin", 8028}, {"ill", 8416}, {"eyes", 10114}, {"weary", 10116}, {"plead", 12542}, {"moyai", 12544},
        {"fire", 16322}, {"flirty", 16324}, {"lightbulb", 16326}, {"moneymouth", 16328}, {"nerd", 16330},
        {"skull", 16332}, {"trophy", 16334}, {"wilted", 16336}
    };

    const growmoji *growmoji_for_code(std::string_view code)
    {
        auto it = std::ranges::find(growmoji_items, code, &growmoji::code);
        return (it == std::end(growmoji_items)) ? nullptr : &*it;
    }
    const growmoji *growmoji_for_item(short item)
    {
        auto it = std::ranges::find(growmoji_items, item, &growmoji::item);
        return (it == std::end(growmoji_items)) ? nullptr : &*it;
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
        const ::peer *pPeer = static_cast<::peer*>(event.peer->data);
        std::vector<std::pair<std::string_view, std::string_view>> free_list{};
        for (const auto &[code, glyph] : emoticon)
            if (growmoji_for_code(code) == nullptr) free_list.emplace_back(code, glyph);
        std::ranges::sort(free_list, {}, &std::pair<std::string_view, std::string_view>::second);

        int unlocked = static_cast<int>(free_list.size()), total = static_cast<int>(free_list.size());
        std::string grid{}, locked{};
        for (const growmoji &g : growmoji_items)
        {
            if (!emoticon.contains(g.code)) continue;
            const bool have = growmoji_unlocked(*pPeer, g.code);
            ++total;
            if (have) ++unlocked;
            const std::string &name = id_to_item(g.item).raw_name;
            if (have) grid += std::format("add_label_with_icon|small|{} `2({})``|left|{}|\n", emoticon[g.code], g.code, g.item);
            else locked += std::format("add_label_with_icon|small|`s({})`` - use a `w{}`` to unlock|left|{}|\n", g.code, name, g.item);
        }
        grid += locked; // @note unlocked first

        std::string free_rows{};
        for (std::size_t i = 0; i < free_list.size(); i += 4)
        {
            std::string row{};
            for (std::size_t j = i; j < std::min(i + 4, free_list.size()); ++j)
                row += std::format("{} `w({})``     ", free_list[j].second, free_list[j].first);
            free_rows += std::format("add_smalltext|{}|left|\n", row);
        }
        send_varlist(event.peer, {
            "OnDialogRequest",
            ::create_dialog()
                .set_default_color("`o")
                .add_label_with_icon("big", "`wGrowmojis``", 5770)
                .add_spacer("small")
                .add_textbox(std::format("You have unlocked `2{}/{}`` Growmojis.", unlocked, total))
                .add_smalltext("Type a Growmoji's name in brackets in chat to use it, like `w(smile)``. Use a Growmoji item on yourself to unlock that Growmoji forever.")
                .add_spacer("small")
                .add_label("small", "`wCollectable Growmojis``")
                .add_raw(grid)
                .add_spacer("small")
                .add_label("small", "`wEveryone has these``")
                .add_raw(free_rows)
                .add_spacer("small")
                .add_quick_exit()
                .end_dialog("growmojis", "Close", "")
        });
    }

    void show_growmoji(ENetEvent &event, std::string_view code)
    {
        const ::peer *pPeer = static_cast<::peer*>(event.peer->data);
        const growmoji *g = growmoji_for_code(code);
        auto glyph = emoticon.find(code);
        if (g == nullptr || glyph == emoticon.end()) return;
        const bool have = growmoji_unlocked(*pPeer, code);
        const std::string &name = id_to_item(g->item).raw_name;
        send_varlist(event.peer, {
            "OnDialogRequest",
            ::create_dialog()
                .set_default_color("`o")
                .add_label_with_icon("big", std::format("`w{}``", name), g->item)
                .add_spacer("small")
                .add_textbox(have ?
                    std::format("`2Unlocked!`` Type `w({})`` in chat to use {}", code, glyph->second) :
                    std::format("`4Locked.`` Find a `w{}`` and use it on yourself to unlock `w({})`` forever.", name, code))
                .add_spacer("small")
                .add_button("back", "Back")
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
    else if (button == "bonus") show_grow_pass(event);
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

bool growmoji_unlocked(const ::peer &peer, std::string_view code)
{
    if (growmoji_for_code(code) == nullptr) return true;
    return peer.growmojis.find(std::format(",{},", code)) != std::string::npos;
}

bool is_growmoji_item(short item) { return growmoji_for_item(item) != nullptr; }

bool growmoji_use(ENetEvent& event, short item)
{
    const growmoji *g = growmoji_for_item(item);
    if (g == nullptr) return false;
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    auto glyph = emoticon.find(g->code);
    if (growmoji_unlocked(*pPeer, g->code))
    {
        on::ConsoleMessage(event.peer, std::format("You already unlocked `w({})``!", g->code));
        return true;
    }
    if (pPeer->growmojis.empty()) pPeer->growmojis = ",";
    pPeer->growmojis += std::format("{},", g->code);
    pPeer->mysql_update("growmojis", pPeer->growmojis);
    modify_item_inventory(event, ::slot(item, -1));
    on::EmoticonDataChanged(event);
    on::ConsoleMessage(event.peer, std::format("`2You unlocked a new Growmoji!`` Type `w({})`` in chat to use it.", g->code));
    send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, std::format("`2New Growmoji: {}``", (glyph != emoticon.end()) ? std::string(glyph->second) : std::format("({})", g->code)), 0u, 1u });
    play_sfx(event.peer, "audio/piano_nice.wav");
    return true;
}

void growmojis_edit(ENetEvent& event, const ::hPipe &hPipe)
{
    const std::string button = hPipe["buttonClicked"];
    if (button.starts_with("gmoji_")) show_growmoji(event, button.substr(6));
    else if (button == "back") show_growmojis(event);
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
    show_grow_pass(event, "daily");
}

void action::claimdailyreward(ENetEvent& event, const std::string&)
{
    show_grow_pass(event, "daily");
}
