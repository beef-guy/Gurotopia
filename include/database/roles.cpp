#include "pch.hpp"

#include "world.hpp"
#include "onVariant/Action.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "commands/__command.hpp"
#include "tools/terminal.hpp"
#include "database/titles.hpp"

#include "roles.hpp"

#include <algorithm>
#include <map>

namespace
{
    std::map<int, role_def> table{};

    /* @note the commands a role can be given: not emotes, not /help */
    bool listed(std::string_view name, const ::cmd_entry &entry)
    {
        return entry.run != on::Action && name != "/help" && name != "/?" && name != "/playermode"; // @note /playermode is the owner's own switch
    }

    /* @note what a server without a roles table (or a gateway) uses */
    const std::map<int, role_def> &builtin()
    {
        static const std::map<int, role_def> defaults = []
        {
            std::map<int, role_def> out{};
            out[PLAYER] = { PLAYER, "Player", 0, "", "", PLAYER, {} };
            out[MODERATOR] = { MODERATOR, "Moderator", 500, "#", "@", MODERATOR, {} };
            out[DEVELOPER] = { DEVELOPER, "Developer", 1000, "6", "@", DEVELOPER, {} };
            for (const auto &[name, entry] : cmd_pool)
                for (auto &[id, role] : out)
                    if (listed(name, entry) && entry.role <= role.powers) role.commands.emplace(name);
            return out;
        }();
        return defaults;
    }

    std::string escape(std::string_view text)
    {
        std::string out(text.size() * 2 + 1, '\0');
        out.resize(mysql_real_escape_string(db.mysql, out.data(), text.data(), text.size()));
        return out;
    }

    std::string join(const std::unordered_set<std::string> &names)
    {
        std::vector<std::string> sorted(names.begin(), names.end());
        std::ranges::sort(sorted);
        std::string out{};
        for (const std::string &name : sorted) out += (out.empty() ? "" : ",") + name;
        return out;
    }

    std::unordered_set<std::string> split(const std::string &text)
    {
        std::unordered_set<std::string> out{};
        std::size_t start = 0;
        while (start <= text.size())
        {
            std::size_t end = text.find(',', start);
            if (end == std::string::npos) end = text.size();
            std::string name = text.substr(start, end - start);
            std::erase_if(name, [](char c) { return std::isspace(static_cast<unsigned char>(c)); });
            if (!name.empty()) out.emplace(name);
            start = end + 1;
        }
        return out;
    }

    std::vector<std::string> names_in_table()
    {
        std::vector<std::string> out{};
        if (mysql_query(db.mysql, "SELECT name FROM panel_commands")) return out;
        if (MYSQL_RES *res = mysql_store_result(db.mysql); res != nullptr)
        {
            while (MYSQL_ROW row = mysql_fetch_row(res)) if (row[0]) out.emplace_back(row[0]);
            mysql_free_result(res);
        }
        return out;
    }

    void load()
    {
        if (mysql_query(db.mysql, "SELECT id, name, priority, color, tag, powers, commands FROM roles")) return;
        MYSQL_RES *res = mysql_store_result(db.mysql);
        if (res == nullptr) return;
        std::map<int, role_def> loaded{};
        while (MYSQL_ROW row = mysql_fetch_row(res))
        {
            role_def role{};
            role.id = row[0] ? std::atoi(row[0]) : 0;
            role.name = row[1] ? row[1] : "";
            role.priority = row[2] ? std::atoi(row[2]) : 0;
            role.color = row[3] ? std::string(row[3]).substr(0, 1) : "";
            role.tag = row[4] ? row[4] : "";
            role.powers = static_cast<u_char>(std::clamp(row[5] ? std::atoi(row[5]) : 0, 0, static_cast<int>(DEVELOPER)));
            role.commands = split(row[6] ? row[6] : "");
            loaded[role.id] = role;
        }
        mysql_free_result(res);
        /* @note the three built-in roles can't go missing, and the owner's role keeps every power */
        for (const auto &[id, role] : builtin()) if (!loaded.contains(id)) loaded[id] = role;
        loaded[DEVELOPER].powers = DEVELOPER;
        loaded[PLAYER].priority = 0;
        table = std::move(loaded);
    }
}

void roles::init()
{
    mysql_query(db.mysql,
        "CREATE TABLE IF NOT EXISTS roles ("
        "id INT NOT NULL PRIMARY KEY," // @note the website picks the next id
        "name VARCHAR(24) NOT NULL,"
        "priority INT NOT NULL DEFAULT 0,"
        "color VARCHAR(1) NOT NULL DEFAULT '',"
        "tag VARCHAR(4) NOT NULL DEFAULT '',"
        "powers INT NOT NULL DEFAULT 0,"
        "commands TEXT"
    ")");
    for (const auto &[id, role] : builtin())
        mysql_query(db.mysql, std::format(
            "INSERT IGNORE INTO roles (id, name, priority, color, tag, powers, commands) VALUES ({}, '{}', {}, '{}', '{}', {}, '{}')",
            id, role.name, role.priority, role.color, escape(role.tag), role.powers, escape(join(role.commands))).c_str());

    /*
     * @note the website lists these. a command that is new in this game build goes to every role
     * whose powers are high enough for it, like it would have before roles could be edited
     */
    mysql_query(db.mysql, "CREATE TABLE IF NOT EXISTS panel_commands (name VARCHAR(32) NOT NULL PRIMARY KEY, min_role INT NOT NULL DEFAULT 0)");
    const std::vector<std::string> known = names_in_table();
    for (const auto &[name, entry] : cmd_pool)
    {
        if (!listed(name, entry)) continue;
        if (!known.empty() && std::ranges::find(known, std::string(name)) == known.end())
            mysql_query(db.mysql, std::format(
                "UPDATE roles SET commands = IF(commands IS NULL OR commands = '', '{0}', CONCAT(commands, ',{0}')) WHERE powers >= {1} AND id <> {2}",
                escape(name), entry.role, static_cast<int>(DEVELOPER)).c_str());
        mysql_query(db.mysql, std::format("REPLACE INTO panel_commands (name, min_role) VALUES ('{}', {})", escape(name), entry.role).c_str());
    }
    for (const std::string &name : known)
        if (!cmd_pool.contains(name)) mysql_query(db.mysql, std::format("DELETE FROM panel_commands WHERE name = '{}'", escape(name)).c_str());
    load();
}

void roles::reload()
{
    load();
    peers("", PeerSearch::EVERYONE, [](ENetPeer *peer)
    {
        const ::peer *pPeer = static_cast<::peer*>(peer->data);
        if (pPeer != nullptr && pPeer->authenticated) apply(peer);
    });
}

const role_def &roles::get(int id)
{
    const std::map<int, role_def> &from = table.empty() ? builtin() : table;
    if (auto it = from.find(id); it != from.end()) return it->second;
    return from.at(PLAYER);
}

const role_def *roles::find(const std::string &name_or_id)
{
    std::string wanted = name_or_id;
    for (char &c : wanted) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (wanted == "mod") wanted = "moderator";
    if (wanted == "dev") wanted = "developer";
    const std::map<int, role_def> &from = table.empty() ? builtin() : table;
    for (const auto &[id, role] : from)
    {
        std::string name = role.name;
        for (char &c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (name == wanted || std::to_string(id) == wanted) return &role;
    }
    return nullptr;
}

bool roles::allows(int id, std::string_view command)
{
    if (id == DEVELOPER) return true;
    return get(id).commands.contains(std::string(command));
}

u_char roles::powers(int id)
{
    return get(id).powers;
}

std::string roles::display_name(const ::peer &peer)
{
    const role_def &role = get(peer.player_mode ? PLAYER : peer.role_id);
    std::string color = "w";
    if (peer.netid != 0 && !peer.recent_worlds.empty())
    {
        if (auto world = std::ranges::find(worlds, peer.recent_worlds.back(), &::world::name); world != worlds.end())
        {
            if (peer.user_id == world->owner) color = "2";
            else if (std::ranges::find(world->access, peer.user_id) != world->access.end()) color = "^";
        }
    }
    if (const std::string title = titles::color(peer); !title.empty()) color = title; // @note Dr. is red, Legend is gold
    if (!role.color.empty()) color = role.color; // @note staff colours still win
    return std::format("`{}{}{}{}{}``", color, role.tag, titles::prefix(peer), peer.growid, titles::suffix(peer));
}

void roles::refresh_name(ENetPeer *peer)
{
    ::peer *pPeer = static_cast<::peer*>(peer->data);
    pPeer->display_growid = display_name(*pPeer);
    if (pPeer->netid == 0 || pPeer->recent_worlds.empty()) return;
    const int netid = pPeer->netid;
    const std::string name = pPeer->display_growid, icon = titles::icon(*pPeer), country = titles::country_state(*pPeer);
    peers(pPeer->recent_worlds.back(), PeerSearch::WORLD, [netid, &name, &icon, &country](ENetPeer *other)
    {
        send_varlist(other, { "OnNameChanged", name, icon }, netid); // @note icon: the title icon JSON, "" takes it away
        send_varlist(other, { "OnCountryState", country }, netid);   // @note title badges (OnNameChanged just reset them)
    });
}

void roles::apply(ENetPeer *peer)
{
    ::peer *pPeer = static_cast<::peer*>(peer->data);
    if (!table.empty() && !table.contains(pPeer->role_id)) pPeer->role_id = PLAYER; // @note their role was deleted
    const u_char level = powers(pPeer->role_id);
    if (pPeer->player_mode) pPeer->real_role = level; // @note /playermode gives this back
    else pPeer->role = level;
    refresh_name(peer);
}
