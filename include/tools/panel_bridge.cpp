#include "pch.hpp"

#include "onVariant/ConsoleMessage.hpp"
#include "action/quit_to_exit.hpp"
#include "commands/moderation.hpp"
#include "tools/terminal.hpp"

#include "panel_bridge.hpp"

#include <chrono>
#include <ctime>

namespace
{
    using clock = std::chrono::steady_clock;
    clock::time_point last_poll{};
    clock::time_point last_live{};

    std::string escape(const std::string &text)
    {
        std::string out(text.size() * 2 + 1, '\0');
        out.resize(mysql_real_escape_string(db.mysql, out.data(), text.c_str(), text.size()));
        return out;
    }

    std::string role_name(int role)
    {
        switch (role)
        {
            case DEVELOPER: return "Developer";
            case MODERATOR: return "Moderator";
            default: return "Player";
        }
    }

    void disconnect(ENetPeer *peer, const std::string &message)
    {
        on::ConsoleMessage(peer, message);
        send_gameMessage(peer, {.message = "action|logon_fail\n"});
        enet_peer_disconnect_later(peer, 0);
    }

    /* @note the website already changed the account in the database. bring the online copy in line with it */
    void player_sync(const std::string &growid)
    {
        ENetPeer *peer = moderation::find_online(growid);
        if (peer == nullptr) return; // @note offline: they get the new values when they log in
        ::peer *pPeer = static_cast<::peer*>(peer->data);

        if (mysql_query(db.mysql, std::format("SELECT role, banned_until, ban_reason, muted_until FROM peer WHERE uid = {}", pPeer->user_id).c_str())) return;
        MYSQL_RES *res = mysql_store_result(db.mysql);
        if (res == nullptr) return;
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row == nullptr) { mysql_free_result(res); return; }
        const int role = row[0] ? std::atoi(row[0]) : 0;
        const long long banned_until = row[1] ? std::atoll(row[1]) : 0;
        const std::string ban_reason = row[2] ? row[2] : "";
        const long long muted_until = row[3] ? std::atoll(row[3]) : 0;
        mysql_free_result(res);

        const long long now = std::time(nullptr);
        pPeer->banned_until = banned_until;
        pPeer->ban_reason = ban_reason;
        if (banned_until < 0 || banned_until > now)
        {
            pPeer->authenticated = false; // @note like a refused login: nothing is saved over the account on the way out
            pPeer->mysql_update_all();    // @note but keep what they had until now
            return disconnect(peer, std::format("`4You were banned{}.``{}",
                banned_until < 0 ? "" : " for " + moderation::duration(banned_until - now),
                ban_reason.empty() ? "" : " Reason: " + ban_reason));
        }

        const bool was_muted = pPeer->muted_until < 0 || pPeer->muted_until > now;
        const bool is_muted = muted_until < 0 || muted_until > now;
        pPeer->muted_until = muted_until;
        if (is_muted && !was_muted) on::ConsoleMessage(peer, std::format("`4You were muted for {}.``", moderation::duration(muted_until < 0 ? -1 : muted_until - now)));
        if (!is_muted && was_muted) on::ConsoleMessage(peer, "`2You can talk again.``");

        const u_char current = pPeer->player_mode ? pPeer->real_role : pPeer->role;
        if (role != current)
        {
            if (pPeer->player_mode) pPeer->real_role = static_cast<u_char>(role); // @note /playermode gives this back
            else pPeer->role = static_cast<u_char>(role);
            on::ConsoleMessage(peer, std::format("`2Your role is now {}.``", role_name(role)));
        }
    }

    void kick(const std::string &growid, const std::string &reason)
    {
        if (ENetPeer *peer = moderation::find_online(growid); peer != nullptr)
            disconnect(peer, std::format("`4You were kicked by staff.``{}", reason.empty() ? "" : " Reason: " + reason));
    }

    /* @note sends everyone in a world back to the world menu. the last one out saves and unloads it */
    void world_empty(const std::string &name, const std::string &message)
    {
        std::vector<ENetPeer*> inside{};
        peers(name, PeerSearch::WORLD, [&inside](ENetPeer *peer) { inside.push_back(peer); });
        for (ENetPeer *peer : inside)
        {
            on::ConsoleMessage(peer, message);
            ENetEvent event{};
            event.peer = peer;
            action::quit_to_exit(event, "", false);
        }
    }

    void world_delete(const std::string &name)
    {
        world_empty(name, "`4This world was deleted by its server's owner.``");
        if (auto it = std::ranges::find(worlds, name, &::world::name); it != worlds.end())
        {
            it->blocks.clear(); // @note save() skips a world with no blocks, so erasing it can't write it back
            worlds.erase(it);
        }
        mysql_query(db.mysql, std::format("DELETE FROM world WHERE name = '{}'", escape(name)).c_str());
    }

    void run(const std::string &action, const std::string &target, const std::string &value)
    {
        if (action == "player_sync") player_sync(target);
        else if (action == "kick") kick(target, value);
        else if (action == "world_empty") world_empty(target, "`4Everyone was sent out of this world by its server's owner.``");
        else if (action == "world_delete") world_delete(target);
        else print(std::format("panel: unknown action {}", action));
    }

    void poll()
    {
        /* @note anything older than a minute was meant for a server that was stopped. it's done by now in the database */
        if (mysql_query(db.mysql,
            "SELECT id, action, target, value, created_at > NOW() - INTERVAL 60 SECOND "
            "FROM panel_actions WHERE done_at IS NULL ORDER BY id LIMIT 20")) return;
        MYSQL_RES *res = mysql_store_result(db.mysql);
        if (res == nullptr) return;

        struct row_t { std::string id, action, target, value; bool fresh; };
        std::vector<row_t> rows{};
        while (MYSQL_ROW row = mysql_fetch_row(res))
            rows.push_back({ row[0] ? row[0] : "0", row[1] ? row[1] : "", row[2] ? row[2] : "", row[3] ? row[3] : "", row[4] && row[4][0] == '1' });
        mysql_free_result(res);

        for (const row_t &row : rows)
        {
            mysql_query(db.mysql, std::format("UPDATE panel_actions SET done_at = NOW() WHERE id = {}", std::atoll(row.id.c_str())).c_str());
            if (!row.fresh) continue;
            try { run(row.action, row.target, row.value); }
            catch (const std::exception &exc) { print(std::format("panel: {} {} failed: {}", row.action, row.target, exc.what())); }
        }
    }

    void write_live()
    {
        std::string values{};
        peers("", PeerSearch::EVERYONE, [&values](ENetPeer *peer)
        {
            const ::peer *pPeer = static_cast<::peer*>(peer->data);
            if (pPeer == nullptr || !pPeer->authenticated || pPeer->growid.empty()) return;
            const std::string world = pPeer->netid == 0 ? "" : pPeer->recent_worlds.back();
            if (!values.empty()) values += ',';
            values += std::format("('{}','{}')", escape(pPeer->growid), escape(world));
        });
        mysql_query(db.mysql, "DELETE FROM panel_live");
        if (!values.empty()) mysql_query(db.mysql, ("INSERT IGNORE INTO panel_live (growid, world) VALUES " + values).c_str());
    }
}

void panel_bridge_create_tables()
{
    mysql_query(db.mysql,
        "CREATE TABLE IF NOT EXISTS panel_actions ("
        "id INT AUTO_INCREMENT PRIMARY KEY,"
        "action VARCHAR(16) NOT NULL,"
        "target VARCHAR(24) NOT NULL,"
        "value VARCHAR(255) DEFAULT '',"
        "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "done_at DATETIME NULL,"
        "INDEX (done_at)"
    ")");
    mysql_query(db.mysql,
        "CREATE TABLE IF NOT EXISTS panel_live ("
        "growid VARCHAR(18) NOT NULL PRIMARY KEY,"
        "world VARCHAR(24) DEFAULT ''"
    ")");
    mysql_query(db.mysql, "DELETE FROM panel_actions WHERE done_at < NOW() - INTERVAL 7 DAY");
}

void panel_tick()
{
    if (db.mysql == nullptr) return;
    const clock::time_point now = clock::now();
    if (now - last_poll >= std::chrono::seconds(1))
    {
        poll();
        last_poll = now;
    }
    if (now - last_live >= std::chrono::seconds(5))
    {
        write_live();
        last_live = now;
    }
}

void panel_bridge_clear()
{
    if (db.mysql != nullptr) mysql_query(db.mysql, "DELETE FROM panel_live");
}
