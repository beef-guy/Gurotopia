#include "pch.hpp"

#include "database.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "tools/sound.hpp"

#include <algorithm>

#include "friends.hpp"

std::vector<::friend_row> friends_of(int uid)
{
    std::vector<::friend_row> rows{};
    if (uid == 0) return rows;

    ::hStmt hStmt{ "SELECT f.friend_uid, p.growid FROM friends f JOIN peer p ON p.uid = f.friend_uid WHERE f.uid = ? ORDER BY p.growid" };
    MYSQL_BIND param = make_bind_in(uid);
    hStmt.bind_param(&param);

    int friend_uid{};
    std::string growid{};
    u_long length{};
    MYSQL_BIND results[2] = { make_bind_out(friend_uid), make_bind_out(growid) };
    results[1].length = &length;
    if (hStmt.pStmt == nullptr || mysql_stmt_bind_result(hStmt.pStmt, results)) return rows;

    hStmt.execute();
    while (mysql_stmt_fetch(hStmt.pStmt) == 0 && rows.size() < MAX_FRIENDS)
        rows.emplace_back(::friend_row{ friend_uid, std::string(growid.data(), std::min<u_long>(length, growid.size())) });

    return rows;
}

void friends_add(int a, int b)
{
    if (a == 0 || b == 0 || a == b) return;

    ::hStmt hStmt{ "INSERT IGNORE INTO friends (uid, friend_uid) VALUES (?, ?), (?, ?)" };
    MYSQL_BIND params[4] = { make_bind_in(a), make_bind_in(b), make_bind_in(b), make_bind_in(a) };
    hStmt.bind_param(params);
    hStmt.execute();
}

void friends_remove(int a, int b)
{
    ::hStmt hStmt{ "DELETE FROM friends WHERE (uid = ? AND friend_uid = ?) OR (uid = ? AND friend_uid = ?)" };
    MYSQL_BIND params[4] = { make_bind_in(a), make_bind_in(b), make_bind_in(b), make_bind_in(a) };
    hStmt.bind_param(params);
    hStmt.execute();
}

ENetPeer *online_by_uid(int uid)
{
    ENetPeer *found = nullptr;
    if (uid == 0) return found;

    peers("", PeerSearch::EVERYONE, [&](ENetPeer *p)
    {
        const ::peer *pOthers = static_cast<::peer*>(p->data);
        if (found == nullptr && pOthers != nullptr && pOthers->authenticated && pOthers->user_id == uid) found = p;
    });
    return found;
}

std::size_t friends_online(const ::peer &peer)
{
    return std::ranges::count_if(peer.friends, [](int uid) { return online_by_uid(uid) != nullptr; });
}

void friends_alert(ENetPeer *peer, bool logged_on)
{
    const ::peer *pPeer = static_cast<::peer*>(peer->data);
    if (pPeer == nullptr || !pPeer->authenticated) return;

    const std::string message = std::format("`3FRIEND ALERT:`` `o{} has {}.", pPeer->growid, logged_on ? "`2logged on``" : "`4logged off``");
    for (int uid : pPeer->friends)
    {
        ENetPeer *other = online_by_uid(uid);
        if (other == nullptr || other == peer) continue;

        on::ConsoleMessage(other, message);
        play_sfx(other, logged_on ? "audio/friend_logon.wav" : "audio/friend_logoff.wav");
    }
}

void show_friends(ENetEvent &event, bool all)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    const std::vector<::friend_row> rows = friends_of(pPeer->user_id);
    pPeer->friends.clear(); // @note the database is the truth, refresh what we keep in memory
    for (const ::friend_row &row : rows) pPeer->friends.emplace_back(row.uid);

    std::string online_list{}, offline_list{};
    std::size_t online{};
    for (const ::friend_row &row : rows)
    {
        ENetPeer *other = online_by_uid(row.uid);
        if (other != nullptr)
        {
            ++online;
            const ::peer *pOthers = static_cast<::peer*>(other->data);
            const std::string where = (pOthers->netid != 0 && !pOthers->recent_worlds.back().empty()) ? 
                std::format(" (`5{}``)", pOthers->recent_worlds.back()) : "";
            online_list.append(std::format("add_button|friend_{}|`2{}``{}|noflags|0|0|\n", row.uid, row.growid, where));
        }
        else if (all) offline_list.append(std::format("add_button|friend_{}|`4{}`` (offline)|noflags|0|0|\n", row.uid, row.growid));
    }

    std::string body{};
    if (rows.empty()) 
        body = "add_textbox|`oYou don't have any friends yet. Wrench a player and choose `2Add as friend``!``|left|\n";
    else if (online == 0 && !all) 
        body = "add_textbox|`oNone of your friends are currently online.``|left|\n";
    else 
        body = online_list + offline_list;

    send_varlist(event.peer, {
        "OnDialogRequest",
        std::format(
            "set_default_color|`o\n"
            "add_label_with_icon|big|{} of {} `wFriends Online``|left|1366|\n"
            "add_spacer|small|\n"
            "{}"
            "add_spacer|small|\n"
            "{}"
            "add_button|back|Back|noflags|0|0|\n"
            "add_button||Close|noflags|0|0|\n"
            "end_dialog|friends|||\n"
            "add_quick_exit|\n",
            online, rows.size(), body,
            all ? "add_button|friend_online|Show online only|noflags|0|0|\n" : "add_button|friend_all|Show offline too|noflags|0|0|\n"
        )
    });
}
