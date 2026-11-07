#include "pch.hpp"

#include "onVariant/ConsoleMessage.hpp"
#include "action/quit_to_exit.hpp"
#include "commands/moderation.hpp"
#include "database/roles.hpp"
#include "database/titles.hpp"
#include "database/item_effects.hpp"
#include "database/machines.hpp"
#include "database/playmods.hpp"
#include "commands/punch.hpp"
#include "tools/terminal.hpp"
#include "scripting/lua.hpp"

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

        if (role != pPeer->role_id)
        {
            pPeer->role_id = role;
            roles::apply(peer); // @note new powers, and everyone in the world sees the new name colour now
            on::ConsoleMessage(peer, std::format("`2Your role is now {}.``", roles::get(role).name));
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

    /* @note the website changed items (item_edits). players get the new items.dat when they log in again */
    void items_reload()
    {
        const int changed = items_apply_edits();
        panel_items_catalog();
        print(std::format("panel: items reloaded, {} changed or added", changed));
        peers("", PeerSearch::EVERYONE, [](ENetPeer *peer)
        {
            const ::peer *pPeer = static_cast<::peer*>(peer->data);
            if (pPeer != nullptr && pPeer->authenticated)
                on::ConsoleMessage(peer, "`2This server's items were just updated.`` Log out and back in to see the changes.");
        });
    }

    void run(const std::string &action, const std::string &target, const std::string &value)
    {
        if (action == "player_sync") player_sync(target);
        else if (action == "kick") kick(target, value);
        else if (action == "world_empty") world_empty(target, "`4Everyone was sent out of this world by its server's owner.``");
        else if (action == "world_delete") world_delete(target);
        else if (action == "roles_reload") roles::reload(); // @note a role was made, edited or deleted on the website
        else if (action == "items_reload") items_reload();
        else if (action == "effects_reload") item_effects::reload(); // @note an item effect was made, edited or deleted on the website
        else if (action == "machines_reload") machines::reload(); // @note a machine's capacity was changed on the website
        else if (action == "titles_reload") titles::reload(); // @note a title was made, edited or deleted on the website
        else if (action == "scripts_reload") lua::reload(); // @note a Lua script was saved, switched on or off, or deleted on the website
        else if (action == "titles_sync") // @note the website gave or took a player's title
        {
            if (ENetPeer *peer = moderation::find_online(target); peer != nullptr) titles::sync_peer(peer);
        }
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
    /* @note the website's changes to items.dat (web/src/items.js). a NULL value is left as items.dat has it */
    mysql_query(db.mysql,
        "CREATE TABLE IF NOT EXISTS item_edits ("
        "id INT NOT NULL PRIMARY KEY,"
        "base_id INT NULL," // @note custom items: the item it's a copy of
        "removed TINYINT NOT NULL DEFAULT 0,"
        "name VARCHAR(64) NULL,"
        "texture VARCHAR(64) NULL,"
        "info TEXT NULL,"
        "rarity INT NULL, hits INT NULL, hit_reset INT NULL, collision INT NULL, cat INT NULL, tex_x INT NULL, tex_y INT NULL,"
        "updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP"
    ")");
    /* @note added later: more of items.dat, and the server's punch effect and powers per item */
    mysql_query(db.mysql,
        "ALTER TABLE item_edits "
        "ADD COLUMN IF NOT EXISTS type INT NULL, ADD COLUMN IF NOT EXISTS hit_sound INT NULL, ADD COLUMN IF NOT EXISTS cloth INT NULL,"
        "ADD COLUMN IF NOT EXISTS spread INT NULL, ADD COLUMN IF NOT EXISTS max_amount INT NULL, ADD COLUMN IF NOT EXISTS grow INT NULL,"
        "ADD COLUMN IF NOT EXISTS seed_base INT NULL, ADD COLUMN IF NOT EXISTS seed_overlay INT NULL,"
        "ADD COLUMN IF NOT EXISTS tree_base INT NULL, ADD COLUMN IF NOT EXISTS tree_leaves INT NULL,"
        "ADD COLUMN IF NOT EXISTS seed_color BIGINT NULL, ADD COLUMN IF NOT EXISTS seed_overlay_color BIGINT NULL,"
        "ADD COLUMN IF NOT EXISTS extra_file VARCHAR(128) NULL, ADD COLUMN IF NOT EXISTS punch INT NULL, ADD COLUMN IF NOT EXISTS powers INT NULL");
}

void panel_items_catalog()
{
    if (db.mysql == nullptr) return;
    /* @note only the website reads it, so it's simply made again (an older server made it with shorter columns) */
    mysql_query(db.mysql, "DROP TABLE IF EXISTS item_catalog");
    mysql_query(db.mysql,
        "CREATE TABLE item_catalog ("
        "id INT NOT NULL PRIMARY KEY,"
        "name VARCHAR(255) NOT NULL DEFAULT '',"
        "type INT NOT NULL DEFAULT 0, rarity INT NOT NULL DEFAULT 0, hits INT NOT NULL DEFAULT 0, hit_reset INT NOT NULL DEFAULT 0,"
        "collision INT NOT NULL DEFAULT 0, cat INT NOT NULL DEFAULT 0, cloth INT NOT NULL DEFAULT 10,"
        "texture VARCHAR(255) NOT NULL DEFAULT '', tex_x INT NOT NULL DEFAULT 0, tex_y INT NOT NULL DEFAULT 0,"
        "info TEXT, custom TINYINT NOT NULL DEFAULT 0,"
        "hit_sound INT NOT NULL DEFAULT 0, spread INT NOT NULL DEFAULT 0, max_amount INT NOT NULL DEFAULT 0, grow INT NOT NULL DEFAULT 0,"
        "seed_base INT NOT NULL DEFAULT 0, seed_overlay INT NOT NULL DEFAULT 0, tree_base INT NOT NULL DEFAULT 0, tree_leaves INT NOT NULL DEFAULT 0,"
        "seed_color BIGINT NOT NULL DEFAULT 0, seed_overlay_color BIGINT NOT NULL DEFAULT 0, extra_file VARCHAR(255) NOT NULL DEFAULT '',"
        "punch INT NOT NULL DEFAULT 0, powers INT NOT NULL DEFAULT 0,"
        "INDEX (name)"
    ") DEFAULT CHARSET=utf8mb4");

    /* @note items.dat text is single bytes, not UTF-8: keep plain ASCII, the rest becomes ? so MariaDB takes every row */
    const auto plain = [](const std::string &text, std::size_t max)
    {
        std::string out = text.substr(0, max);
        for (char &c : out) if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7e) c = '?';
        return escape(out);
    };
    const std::size_t original = items_original_count();
    const auto row = [&](const ::item &item)
    {
        const ::playmod *mod = get_playmod(item.id);
        return std::format("({},'{}',{},{},{},{},{},{},{},'{}',{},{},'{}',{},{},{},{},{},{},{},{},{},{},{},'{}',{},{})",
            item.id, plain(item.raw_name, 255), item.type, item.rarity, item.hits, item.hit_reset, item.collision, item.cat, item.cloth_type,
            plain(item.texture, 255), item.tex_x, item.tex_y, plain(item.info, 4000), item.id >= original ? 1 : 0,
            item.hit_sound, item.spread, item.max_amount, item.tick, item.seed_base, item.seed_overlay, item.tree_base, item.tree_leaves,
            item.seed_color, item.seed_overlay_color, plain(item.extra_file, 255), get_punch_id(item.id), mod ? mod->mods : 0);
    };
    const std::string insert = "INSERT INTO item_catalog (id, name, type, rarity, hits, hit_reset, collision, cat, cloth, texture, tex_x, tex_y, info, custom, "
        "hit_sound, spread, max_amount, grow, seed_base, seed_overlay, tree_base, tree_leaves, seed_color, seed_overlay_color, extra_file, punch, powers) VALUES ";

    std::size_t written = 0, failed = 0;
    for (std::size_t from = 0; from < items.size(); from += 500)
    {
        const std::size_t to = std::min(items.size(), from + 500);
        std::string values{};
        for (std::size_t i = from; i < to; ++i) values += (values.empty() ? "" : ",") + row(items[i]);
        if (mysql_query(db.mysql, (insert + values).c_str()) == 0) { written += to - from; continue; }
        /* @note one bad row shouldn't cost the other 499: try them one at a time and say which failed */
        print(std::format("item list: rows {}-{} failed together ({}), trying one by one", from, to - 1, mysql_error(db.mysql)));
        for (std::size_t i = from; i < to; ++i)
        {
            if (mysql_query(db.mysql, (insert + row(items[i])).c_str()) == 0) ++written;
            else { ++failed; print(std::format("item list: item {} left out: {}", items[i].id, mysql_error(db.mysql))); }
        }
    }
    if (failed) print(std::format("item list: {} of {} items written for the website", written, items.size()));
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
