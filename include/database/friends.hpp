#pragma once

/* @note friends are kept in the `friends` table, one row per direction (a->b and b->a) */

constexpr std::size_t MAX_FRIENDS = 200; // @note same limit as the real game

struct friend_row {
    int uid{};
    std::string growid{};
};

/* @return every friend of `uid` from the database, sorted by name */
extern std::vector<::friend_row> friends_of(int uid);

/* @note save that `a` and `b` are friends (both ways) */
extern void friends_add(int a, int b);
/* @note forget that `a` and `b` are friends (both ways) */
extern void friends_remove(int a, int b);

/* @return the online player with this user id, or nullptr */
extern ENetPeer *online_by_uid(int uid);

/* @return how many of this player's friends are online right now */
extern std::size_t friends_online(const ::peer &peer);

/* @note "FRIEND ALERT: x has logged on/off." to every online friend, with the log-on/off sound */
extern void friends_alert(ENetPeer *peer, bool logged_on);

/* @note the friends list dialog. `all` also lists friends who are offline */
extern void show_friends(ENetEvent &event, bool all);
