#include "pch.hpp"

#include "onVariant/ConsoleMessage.hpp"
#include "commands/moderation.hpp"
#include "database/roles.hpp"
#include "tools/create_dialog.hpp"
#include "tools/sound.hpp"

#include "titles.hpp"

#include <algorithm>
#include <map>

namespace
{
    std::map<int, title_def> table{};

    /* @note the badge words the client reads from OnCountryState (gtps-guide functions/OnCountryState.md) */
    const std::vector<std::string> badge_words{ "maxLevel", "doctor", "donor", "master", "ccBadge", "ttBadge", "euphoriaTitle" };

    /*
     * @note the real game's titles. The icons are the ones those titles use in the game's own tile sheets.
     * The server's owner can change any of them (or switch them off) in the Title Manager.
     */
    const std::vector<title_def> &builtin()
    {
        static const std::vector<title_def> list{
            { 1, "Doctor", "Real game: finish surgery training (Surgeon role). Puts a red Dr. in front of your name.", "Dr.", "", "4", "doctor", "", 0, 0, "manual", 0, true, 10 },
            { 2, "Legendary", "Real game: finish the Legendary Quest. Adds \"of Legend\" after your name and shows the Legend flag.", "", " of Legend", "9", "", "", 0, 0, "manual", 0, true, 900 },
            { 3, "Max Level", "Reach level 125. Shows the Max Level badge by your name.", "", "", "", "maxLevel", "", 0, 0, "level", 125, true, 20 },
            { 4, "Grow4Good", "Real game: Grow4Good charity event. Swaps your flag for the Grow4Good badge.", "", "", "", "donor", "", 0, 0, "manual", 0, true, 30 },
            { 5, "Mentor", "Real game: Mentor programme. Swaps your flag for the Mentor badge.", "", "", "", "master", "", 0, 0, "manual", 0, true, 40 },
            { 6, "Content Creator", "Real game: official content creators. Swaps your flag for the Content Creator badge.", "", "", "", "ccBadge", "", 0, 0, "manual", 0, true, 50 },
            { 7, "TikTok Creator", "Real game: official TikTok creators. Swaps your flag for the TikTok badge.", "", "", "", "ttBadge", "", 0, 0, "manual", 0, true, 60 },
            { 8, "Party Animal", "Real game: Euphoria event. Shows the Party Animal badge.", "", "", "", "euphoriaTitle", "", 0, 0, "manual", 0, true, 70 },
            { 9, "Thanksgiving", "Real game: Thanksgiving event. Shows a turkey icon by your name.", "", "", "", "", "game/tiles_page15.rttex", 12, 1, "manual", 0, true, 100 },
            { 10, "Old Timer", "Real game: anniversary event for old accounts. Shows an icon by your name.", "", "", "", "", "game/tiles_page7.rttex", 8, 6, "manual", 0, true, 110 },
            { 11, "Winter Santa Claus", "Real game: WinterFest. Shows a Santa icon by your name.", "", "", "", "", "game/tiles_page2.rttex", 28, 8, "manual", 0, true, 120 },
            { 12, "Gold Grow Royal", "Real game: top Grow Pass tier. Shows a gold crown by your name.", "", "", "", "", "game/tiles_page16.rttex", 0, 25, "manual", 0, true, 130 },
            { 13, "Silver Grow Royal", "Real game: Grow Pass. Shows a silver crown by your name.", "", "", "", "", "game/tiles_page16.rttex", 1, 25, "manual", 0, true, 140 },
            { 14, "Bronze Grow Royal", "Real game: Grow Pass. Shows a bronze crown by your name.", "", "", "", "", "game/tiles_page16.rttex", 2, 25, "manual", 0, true, 150 },
            { 15, "Award Winning", "Real game: award winners. Shows a trophy icon by your name.", "", "", "", "", "game/tiles_page16.rttex", 27, 11, "manual", 0, true, 160 },
        };
        return list;
    }

    std::string escape(std::string_view text)
    {
        std::string out(text.size() * 2 + 1, '\0');
        out.resize(mysql_real_escape_string(db.mysql, out.data(), text.data(), text.size()));
        return out;
    }

    std::string lower(std::string text)
    {
        for (char &c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return text;
    }

    /* @note text that goes into a name or a dialog line: no |, no new lines */
    std::string clean(std::string text, std::size_t max)
    {
        std::erase_if(text, [](char c) { return c == '|' || c == '\n' || c == '\r' || c == '\\' || c == '"'; });
        return text.substr(0, max);
    }

    /* @note a texture path for the icon JSON: game/<file>.rttex with plain characters only */
    std::string clean_texture(std::string path)
    {
        const bool ok = !path.empty() && path.size() <= 96 && path.ends_with(".rttex") && path.find("..") == std::string::npos &&
            std::ranges::all_of(path, [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '/' || c == '.' || c == '-'; });
        return ok ? path : "";
    }

    void load()
    {
        if (mysql_query(db.mysql, "SELECT id, name, description, prefix, suffix, color, badge, texture, tex_x, tex_y, grant_type, grant_value, enabled, sort FROM titles")) return;
        MYSQL_RES *res = mysql_store_result(db.mysql);
        if (res == nullptr) return;
        std::map<int, title_def> loaded{};
        while (MYSQL_ROW row = mysql_fetch_row(res))
        {
            title_def t{};
            t.id = row[0] ? std::atoi(row[0]) : 0;
            t.name = clean(row[1] ? row[1] : "", 32);
            t.description = clean(row[2] ? row[2] : "", 160);
            t.prefix = clean(row[3] ? row[3] : "", 24);
            t.suffix = clean(row[4] ? row[4] : "", 24);
            t.color = clean(row[5] ? row[5] : "", 1);
            t.badge = row[6] ? row[6] : "";
            if (std::ranges::find(badge_words, t.badge) == badge_words.end()) t.badge.clear();
            t.texture = clean_texture(row[7] ? row[7] : "");
            t.tex_x = std::clamp(row[8] ? std::atoi(row[8]) : 0, 0, 63);
            t.tex_y = std::clamp(row[9] ? std::atoi(row[9]) : 0, 0, 63);
            t.grant = row[10] ? row[10] : "manual";
            t.grant_value = row[11] ? std::atoi(row[11]) : 0;
            t.enabled = row[12] && row[12][0] == '1';
            t.sort = row[13] ? std::atoi(row[13]) : 0;
            if (t.id > 0 && !t.name.empty()) loaded[t.id] = t;
        }
        mysql_free_result(res);
        table = std::move(loaded);
    }

    /* @return every title, in the order they're drawn (prefixes left to right, " of Legend" last) */
    std::vector<const title_def*> ordered()
    {
        std::vector<const title_def*> out{};
        if (table.empty()) for (const title_def &t : builtin()) out.push_back(&t);
        else for (const auto &[id, t] : table) out.push_back(&t);
        std::ranges::stable_sort(out, [](const title_def *a, const title_def *b) { return (a->sort != b->sort) ? a->sort < b->sort : a->id < b->id; });
        return out;
    }

    bool has_level(const ::peer &peer, int level) { return static_cast<int>(peer.level[0]) >= level; }

    /* @return what owning a title takes, for the menu */
    std::string how_to_get(const title_def &t)
    {
        if (t.grant == "everyone") return "Everyone has this title.";
        if (t.grant == "level") return std::format("Reach level {} to earn it.", t.grant_value);
        if (t.grant == "role") return std::format("Comes with the {} role.", roles::get(t.grant_value).name);
        return "Given by the server's staff.";
    }

    /* @return the player's name the way it looks with only this title on, for the menu */
    std::string preview(const ::peer &peer, const title_def &t)
    {
        const std::string color = t.color.empty() ? "w" : t.color;
        return std::format("`{}{}{}{}``", color, t.prefix, peer.growid, t.suffix);
    }

    std::string effects(const title_def &t)
    {
        std::vector<std::string> parts{};
        if (!t.badge.empty())
        {
            if (t.badge == "maxLevel") parts.emplace_back("the Max Level badge");
            else if (t.badge == "doctor") parts.emplace_back("the doctor badge");
            else if (t.badge == "euphoriaTitle") parts.emplace_back("the Party Animal badge");
            else parts.emplace_back("its badge in place of your flag");
        }
        if (!t.texture.empty()) parts.emplace_back("an icon by your name");
        if (t.suffix.ends_with(" of Legend")) parts.emplace_back("the Legend flag");
        if (parts.empty()) return "";
        std::string out = "Shows ";
        for (std::size_t i = 0; i < parts.size(); ++i) out += (i == 0 ? "" : (i + 1 == parts.size() ? " and " : ", ")) + parts[i];
        return out + ".";
    }

    void save_row(int uid, int title, bool granted, bool equipped)
    {
        mysql_query(db.mysql, std::format(
            "INSERT INTO peer_titles (uid, title, granted, equipped) VALUES ({}, {}, {}, {}) "
            "ON DUPLICATE KEY UPDATE granted = VALUES(granted), equipped = VALUES(equipped)",
            uid, title, granted ? 1 : 0, equipped ? 1 : 0).c_str());
    }

    void read_rows(::peer &peer)
    {
        peer.title_rows.clear();
        if (peer.user_id == 0) return;
        if (mysql_query(db.mysql, std::format("SELECT title, granted, equipped FROM peer_titles WHERE uid = {}", peer.user_id).c_str())) return;
        MYSQL_RES *res = mysql_store_result(db.mysql);
        if (res == nullptr) return;
        while (MYSQL_ROW row = mysql_fetch_row(res))
            if (row[0]) peer.title_rows[std::atoi(row[0])] = { row[1] && row[1][0] == '1', !row[2] || row[2][0] == '1' };
        mysql_free_result(res);
    }
}

void titles::init()
{
    mysql_query(db.mysql,
        "CREATE TABLE IF NOT EXISTS titles ("
        "id INT NOT NULL PRIMARY KEY," // @note 1-99 are the real game's titles, the website gives new ones 100 and up
        "name VARCHAR(32) NOT NULL,"
        "description VARCHAR(160) NOT NULL DEFAULT '',"
        "prefix VARCHAR(24) NOT NULL DEFAULT '',"
        "suffix VARCHAR(24) NOT NULL DEFAULT '',"
        "color VARCHAR(1) NOT NULL DEFAULT '',"
        "badge VARCHAR(16) NOT NULL DEFAULT '',"
        "texture VARCHAR(96) NOT NULL DEFAULT '',"
        "tex_x INT NOT NULL DEFAULT 0,"
        "tex_y INT NOT NULL DEFAULT 0,"
        "icon_item INT NOT NULL DEFAULT 0," // @note the website's own note: which item's picture the icon was copied from
        "grant_type VARCHAR(8) NOT NULL DEFAULT 'manual',"
        "grant_value INT NOT NULL DEFAULT 0,"
        "enabled TINYINT NOT NULL DEFAULT 1,"
        "sort INT NOT NULL DEFAULT 100,"
        "builtin TINYINT NOT NULL DEFAULT 0"
    ")");
    mysql_query(db.mysql,
        "CREATE TABLE IF NOT EXISTS peer_titles ("
        "uid INT NOT NULL,"
        "title INT NOT NULL,"
        "granted TINYINT NOT NULL DEFAULT 0," // @note given by staff or the website
        "equipped TINYINT NOT NULL DEFAULT 1," // @note ticked in the Titles menu
        "PRIMARY KEY (uid, title)"
    ")");
    for (const title_def &t : builtin())
        mysql_query(db.mysql, std::format(
            "INSERT IGNORE INTO titles (id, name, description, prefix, suffix, color, badge, texture, tex_x, tex_y, grant_type, grant_value, enabled, sort, builtin) "
            "VALUES ({}, '{}', '{}', '{}', '{}', '{}', '{}', '{}', {}, {}, '{}', {}, 1, {}, 1)",
            t.id, escape(t.name), escape(t.description), escape(t.prefix), escape(t.suffix), escape(t.color), escape(t.badge),
            escape(t.texture), t.tex_x, t.tex_y, escape(t.grant), t.grant_value, t.sort).c_str());
    load();
}

void titles::reload()
{
    load();
    peers("", PeerSearch::EVERYONE, [](ENetPeer *peer)
    {
        const ::peer *pPeer = static_cast<::peer*>(peer->data);
        if (pPeer != nullptr && pPeer->authenticated) refresh(peer);
    });
}

void titles::load_peer(::peer &peer)
{
    read_rows(peer);
}

void titles::sync_peer(ENetPeer *peer)
{
    ::peer *pPeer = static_cast<::peer*>(peer->data);
    const std::vector<const title_def*> before = shown(*pPeer);
    std::vector<int> had{};
    for (const title_def *t : ordered()) if (owns(*pPeer, *t)) had.push_back(t->id);
    read_rows(*pPeer);
    for (const title_def *t : ordered())
    {
        const bool has = owns(*pPeer, *t), did = std::ranges::find(had, t->id) != had.end();
        if (has && !did) on::ConsoleMessage(peer, std::format("`2You were given the {} title!`` Wrench yourself and tap Title to show or hide it.", t->name));
        if (!has && did) on::ConsoleMessage(peer, std::format("`4Your {} title was taken away.``", t->name));
    }
    refresh(peer);
}

const title_def *titles::find(const std::string &name_or_id)
{
    const std::string wanted = lower(name_or_id);
    for (const title_def *t : ordered())
        if (lower(t->name) == wanted || std::to_string(t->id) == wanted) return t;
    for (const title_def *t : ordered()) // @note "dr", "legend": the start of a name is enough
        if (!wanted.empty() && lower(t->name).starts_with(wanted)) return t;
    return nullptr;
}

bool titles::owns(const ::peer &peer, const title_def &title)
{
    if (!title.enabled) return false;
    if (auto row = peer.title_rows.find(title.id); row != peer.title_rows.end() && row->second.first) return true;
    if (title.grant == "everyone") return true;
    if (title.grant == "level") return has_level(peer, title.grant_value);
    if (title.grant == "role") return peer.role_id == title.grant_value;
    return false;
}

std::vector<const title_def*> titles::shown(const ::peer &peer)
{
    std::vector<const title_def*> out{};
    for (const title_def *t : ordered())
    {
        if (!owns(peer, *t)) continue;
        auto row = peer.title_rows.find(t->id);
        if (row == peer.title_rows.end() || row->second.second) out.push_back(t); // @note a title is on until they switch it off
    }
    return out;
}

std::string titles::color(const ::peer &peer)
{
    for (const title_def *t : shown(peer)) if (!t->color.empty()) return t->color;
    return "";
}

std::string titles::prefix(const ::peer &peer)
{
    std::string out{};
    for (const title_def *t : shown(peer)) out += t->prefix;
    return out;
}

std::string titles::suffix(const ::peer &peer)
{
    /* @note " of Legend" has to be the very end of the name, or the client won't show the Legend flag */
    std::string out{}, legend{};
    for (const title_def *t : shown(peer))
    {
        if (t->suffix.ends_with(" of Legend")) legend = t->suffix;
        else out += t->suffix;
    }
    return out + legend;
}

std::string titles::country_state(const ::peer &peer)
{
    std::string out = peer.country.empty() ? "us" : peer.country;
    std::vector<std::string> added{};
    for (const title_def *t : shown(peer))
        if (!t->badge.empty() && std::ranges::find(added, t->badge) == added.end())
        {
            added.push_back(t->badge);
            out += "|" + t->badge;
        }
    return out;
}

std::string titles::icon(const ::peer &peer)
{
    for (const title_def *t : shown(peer))
        if (!t->texture.empty())
            return std::format(R"({{"PlayerWorldID":{},"TitleTexture":"{}","TitleTextureCoordinates":"{},{}"}})", peer.netid, t->texture, t->tex_x, t->tex_y);
    return "";
}

void titles::refresh(ENetPeer *peer)
{
    roles::refresh_name(peer); // @note the name, its icon and badges, to everyone in their world
}

bool titles::grant(const std::string &growid, const title_def &title, bool on)
{
    if (ENetPeer *online = moderation::find_online(growid); online != nullptr)
    {
        ::peer *pPeer = static_cast<::peer*>(online->data);
        auto &row = pPeer->title_rows[title.id];
        row.first = on;
        if (on) row.second = true; // @note a title that was just given shows right away, like in the real game
        save_row(pPeer->user_id, title.id, row.first, row.second);
        on::ConsoleMessage(online, on
            ? std::format("`2You were given the {} title!`` Wrench yourself and tap Title to show or hide it.", title.name)
            : std::format("`4Your {} title was taken away.``", title.name));
        if (on) play_sfx(online, "audio/achievement.wav");
        refresh(online);
        return true;
    }
    ::peer offline{};
    if (!offline.exists(growid)) return false;
    offline.growid = growid;
    const int uid = offline.mysql_select<signed>("uid");
    if (uid <= 0) return false;
    if (on) save_row(uid, title.id, true, true);
    else mysql_query(db.mysql, std::format("UPDATE peer_titles SET granted = 0 WHERE uid = {} AND title = {}", uid, title.id).c_str());
    return true;
}

void titles::leveled_up(ENetPeer *peer, int old_level, int new_level)
{
    bool earned = false;
    for (const title_def *t : ordered())
    {
        if (!t->enabled || t->grant != "level" || t->grant_value <= old_level || t->grant_value > new_level) continue;
        on::ConsoleMessage(peer, std::format("`2You earned the {} title!`` Wrench yourself and tap Title to show or hide it.", t->name));
        earned = true;
    }
    if (earned) refresh(peer);
}

void titles::show_menu(ENetEvent &event)
{
    const ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::create_dialog dialog = ::create_dialog()
        .set_default_color("`o")
        .add_label_with_icon("big", "`wTitles``", 1794)
        .add_spacer("small");

    std::string list{};
    int count = 0;
    for (const title_def *t : ordered())
    {
        if (!owns(*pPeer, *t)) continue;
        auto row = pPeer->title_rows.find(t->id);
        const bool on = row == pPeer->title_rows.end() || row->second.second;
        list += std::format("add_checkbox|title_{}|{} - {}|{}|\n", t->id, t->name, preview(*pPeer, *t), on ? 1 : 0);
        const std::string about = t->description.empty() ? effects(*t) : t->description;
        if (!about.empty()) list += std::format("add_smalltext|{}|left|\n", about);
        ++count;
    }
    if (count == 0)
    {
        dialog.add_textbox("You don't have any titles yet.")
            .add_smalltext("Titles change how your name looks to everyone, like the red Dr. in front of a doctor's name or \"of Legend\" after a legend's.");
        std::string ways{};
        for (const title_def *t : ordered())
            if (t->enabled && t->grant != "manual") ways += std::format("add_smalltext|`w{}``: {}|left|\n", t->name, how_to_get(*t));
        if (!ways.empty()) dialog.add_spacer("small").add_raw(ways);
        dialog.add_spacer("small").add_quick_exit();
        send_varlist(event.peer, { "OnDialogRequest", dialog.end_dialog("title_edit", "Close", "") });
        return;
    }
    dialog.add_textbox("Tick the titles you want to show. Everyone sees them on your name.")
        .add_smalltext("Only one title icon can show at a time.")
        .add_spacer("small")
        .add_raw(list)
        .add_spacer("small")
        .add_quick_exit();
    send_varlist(event.peer, { "OnDialogRequest", dialog.end_dialog("title_edit", "Cancel", "Apply") });
}

void titles::menu_return(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    std::vector<std::pair<const title_def*, bool>> picked{};
    for (const title_def *t : ordered())
    {
        if (!owns(*pPeer, *t)) continue;
        const std::string value = hPipe[std::format("title_{}", t->id)];
        if (value.empty()) continue; // @note not in the dialog they saw
        picked.emplace_back(t, value == "1");
    }
    if (picked.empty()) return;

    /* @note one icon at a time: keep the icon title they just ticked, or else the first one */
    const title_def *icon_on = nullptr;
    for (const auto &[t, on] : picked)
    {
        if (!on || t->texture.empty()) continue;
        auto row = pPeer->title_rows.find(t->id);
        const bool was_on = row == pPeer->title_rows.end() || row->second.second;
        if (icon_on == nullptr || !was_on) icon_on = t;
    }
    bool dropped = false;
    for (auto &[t, on] : picked)
        if (on && !t->texture.empty() && t != icon_on) { on = false; dropped = true; }

    bool changed = false;
    for (const auto &[t, on] : picked)
    {
        auto &row = pPeer->title_rows[t->id]; // @note a new row is {not given, on}: owned through a rule
        if (row.second != on) changed = true;
        row.second = on;
        save_row(pPeer->user_id, t->id, row.first, row.second);
    }
    if (dropped) on::ConsoleMessage(event.peer, std::format("Only one title icon can show at a time, so you're showing {}.", icon_on->name));
    if (changed)
    {
        refresh(event.peer);
        on::ConsoleMessage(event.peer, "`2Your titles were updated.``");
    }
}

void granttitle(ENetEvent &event, const std::string_view text)
{
    const ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const std::vector<std::string> list = readch(std::string(command_arg(text)), ' ');
    if (list.size() < 2ull)
    {
        std::string names{};
        for (const title_def *t : ordered()) if (t->enabled) names += (names.empty() ? "" : ", ") + t->name;
        on::ConsoleMessage(event.peer, std::format("Usage: /granttitle <`$name``> <`$title``>. Titles: {}", names));
        return;
    }
    std::string wanted{};
    for (std::size_t i = 1; i < list.size(); ++i) wanted += (i == 1 ? "" : " ") + list[i];
    const title_def *title = titles::find(wanted);
    if (title == nullptr) return on::ConsoleMessage(event.peer, std::format("`4There's no title called {}.``", wanted));
    if (!titles::grant(list[0], *title, true)) return on::ConsoleMessage(event.peer, std::format("`4No account called {}.``", list[0]));
    moderation::log(*pPeer, "granttitle", list[0], title->name);
    on::ConsoleMessage(event.peer, std::format("`2{} now has the {} title.``", list[0], title->name));
}

void removetitle(ENetEvent &event, const std::string_view text)
{
    const ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const std::vector<std::string> list = readch(std::string(command_arg(text)), ' ');
    if (list.size() < 2ull) return on::ConsoleMessage(event.peer, "Usage: /removetitle <`$name``> <`$title``>");
    std::string wanted{};
    for (std::size_t i = 1; i < list.size(); ++i) wanted += (i == 1 ? "" : " ") + list[i];
    const title_def *title = titles::find(wanted);
    if (title == nullptr) return on::ConsoleMessage(event.peer, std::format("`4There's no title called {}.``", wanted));
    if (!titles::grant(list[0], *title, false)) return on::ConsoleMessage(event.peer, std::format("`4No account called {}.``", list[0]));
    moderation::log(*pPeer, "removetitle", list[0], title->name);
    on::ConsoleMessage(event.peer, std::format("`2{} no longer has the {} title{}.``", list[0], title->name,
        title->grant == "manual" ? "" : std::format(" (they can still earn it: {})", how_to_get(*title))));
}
