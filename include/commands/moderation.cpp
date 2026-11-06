#include "pch.hpp"

#include "onVariant/ConsoleMessage.hpp"
#include "onVariant/SetBux.hpp"
#include "onVariant/SetClothing.hpp"
#include "tools/sound.hpp"
#include "action/join_request.hpp"
#include "action/quit_to_exit.hpp"
#include "database/mail.hpp"

#include "moderation.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>

namespace
{
    std::string lower(std::string text)
    {
        for (char &c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return text;
    }

    void reply(ENetEvent &event, const std::string &message)
    {
        send_gameMessage(event.peer, {.message = std::format("action|log\nmsg|{}", message)});
    }

    /* @return the words after the command, e.g. "/ban bob 60 spam" -> {"bob", "60", "spam"} */
    std::vector<std::string> words(std::string_view text)
    {
        std::vector<std::string> out{};
        for (std::string &word : readch(std::string{ command_arg(text) }, ' '))
            if (!word.empty()) out.emplace_back(std::move(word));
        return out;
    }

    /* @return everything from word `from` on, joined with spaces */
    std::string rest(const std::vector<std::string> &list, std::size_t from)
    {
        std::string out{};
        for (std::size_t i = from; i < list.size(); ++i)
        {
            if (!out.empty()) out += ' ';
            out += list[i];
        }
        return out;
    }

    /* @return minutes from "60", "1h", "2d" or "perm" (-1). 0 when it can't be read */
    long long read_minutes(const std::string &text)
    {
        if (text == "perm" || text == "permanent") return -1;
        if (text.empty()) return 0;
        long long multiplier = 1;
        std::string number = text;
        switch (std::tolower(static_cast<unsigned char>(text.back())))
        {
            case 'm': multiplier = 1;       number.pop_back(); break;
            case 'h': multiplier = 60;      number.pop_back(); break;
            case 'd': multiplier = 60 * 24; number.pop_back(); break;
        }
        if (number.empty() || !std::ranges::all_of(number, ::isdigit) || number.size() > 7) return 0;
        return std::stoll(number) * multiplier;
    }

    /* @note keep the reason from breaking dialogs or the log line */
    std::string clean(std::string text, std::size_t max)
    {
        std::erase_if(text, [](char c) { return c == '|' || c == '\n' || c == '\r'; });
        if (text.size() > max) text.resize(max);
        return text;
    }

    /* @return true for 127.x, 10.x, 192.168.x and 172.16-31.x */
    bool local_address(const ENetAddress &address)
    {
        const enet_uint8 *ip = address.host.v4;
        return ip[0] == 127 || ip[0] == 10 || (ip[0] == 192 && ip[1] == 168) || (ip[0] == 172 && ip[1] >= 16 && ip[1] <= 31);
    }

    /* @return false (after telling the actor) when they may not act on someone with that role */
    bool outranks(ENetEvent &event, const ::peer &actor, u_char target_role)
    {
        if (actor.role > target_role) return true;
        reply(event, "`4You can't do that to someone with the same or a higher role.``");
        return false;
    }
}

ENetPeer *moderation::find_online(const std::string &growid)
{
    const std::string wanted = lower(growid);
    ENetPeer *found = nullptr;
    peers("", PeerSearch::EVERYONE, [&](ENetPeer *peer)
    {
        const ::peer *pOthers = static_cast<::peer*>(peer->data);
        if (found == nullptr && pOthers != nullptr && pOthers->authenticated && lower(pOthers->growid) == wanted) found = peer;
    });
    return found;
}

std::string moderation::ip_string(const ENetAddress &address)
{
    return std::format("{}.{}.{}.{}", address.host.v4[0], address.host.v4[1], address.host.v4[2], address.host.v4[3]);
}

bool moderation::ip_banned(const ENetAddress &address, std::string &reason, long long &until)
{
    ::hStmt hStmt{ "SELECT until, reason FROM ip_bans WHERE ip = ? AND (until < 0 OR until > ?) LIMIT 1" };
    const std::string ip = ip_string(address);
    const long long now = std::time(nullptr);
    MYSQL_BIND params[2] = { make_bind_in(ip), make_bind_in(now) };
    hStmt.bind_param(params);

    until = 0;
    u_long reason_length = 0;
    MYSQL_BIND results[2] = { make_bind_out(until), make_bind_out(reason) };
    results[1].length = &reason_length;
    if (hStmt.pStmt == nullptr) return false;
    mysql_stmt_bind_result(hStmt.pStmt, results);

    hStmt.execute();
    if (mysql_stmt_store_result(hStmt.pStmt) != 0 || mysql_stmt_num_rows(hStmt.pStmt) == 0) return false;
    hStmt.fetch();
    reason.resize(std::min<u_long>(reason_length, reason.size()));
    return true;
}

void moderation::log(const ::peer &actor, const std::string &action, const std::string &target, const std::string &detail)
{
    ::hStmt hStmt{ "INSERT INTO mod_log (actor, action, target, detail) VALUES (?, ?, ?, ?)" };
    const std::string detail_cut = clean(detail, 255);
    MYSQL_BIND params[4] = { make_bind_in(actor.growid), make_bind_in(action), make_bind_in(target), make_bind_in(detail_cut) };
    hStmt.bind_param(params);
    hStmt.execute();
    print(std::format("[mod] {} {} {} {}", actor.growid, action, target, detail_cut));
}

std::string moderation::duration(long long seconds)
{
    if (seconds < 0) return "forever";
    const long long minutes = (seconds + 59) / 60;
    if (minutes < 60) return std::format("{} minute{}", minutes, minutes == 1 ? "" : "s");
    const long long hours = (minutes + 59) / 60;
    if (hours < 48) return std::format("{} hour{}", hours, hours == 1 ? "" : "s");
    const long long days = (hours + 23) / 24;
    return std::format("{} days", days);
}

void kick(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const std::vector<std::string> list = words(text);
    if (list.empty()) return reply(event, "Usage: /kick <`$name``> [reason]");

    ENetPeer *target = moderation::find_online(list[0]);
    if (target == nullptr) return reply(event, std::format("`4{}`` is not online.", list[0]));
    ::peer *pTarget = static_cast<::peer*>(target->data);
    if (target == event.peer || !outranks(event, *pPeer, pTarget->role)) return;

    const std::string reason = clean(rest(list, 1), 100);
    on::ConsoleMessage(target, std::format("`4You were kicked by staff.``{}", reason.empty() ? "" : " Reason: " + reason));
    send_gameMessage(target, {.message = "action|logon_fail\n"});
    enet_peer_disconnect_later(target, 0);

    moderation::log(*pPeer, "kick", pTarget->growid, reason);
    reply(event, std::format("`2Kicked {}.``", pTarget->growid));
}

void ban(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const std::vector<std::string> list = words(text);
    if (list.size() < 2ull) return reply(event, "Usage: /ban <`$name``> <`$minutes``, `$2h``, `$7d`` or `$perm``> [reason] - also bans their IP while they're online");

    const long long minutes = read_minutes(list[1]);
    if (minutes == 0) return reply(event, "`4The time must look like 30, 2h, 7d or perm.``");
    const long long until = (minutes < 0) ? -1 : std::time(nullptr) + minutes * 60;
    const std::string reason = clean(rest(list, 2), 100);

    ENetPeer *target = moderation::find_online(list[0]);
    std::string target_name = list[0];
    if (target != nullptr)
    {
        ::peer *pTarget = static_cast<::peer*>(target->data);
        if (target == event.peer || !outranks(event, *pPeer, pTarget->role)) return;
        target_name = pTarget->growid;

        pTarget->banned_until = until;
        pTarget->ban_reason = reason;
        pTarget->mysql_update("banned_until", until);
        pTarget->mysql_update("ban_reason", reason);

        /* @note the IP too, so a new account doesn't get around it. not 127.0.0.1 / LAN: everyone testing on one PC shares it */
        if (!local_address(target->address))
        {
            ::hStmt hStmt{ "REPLACE INTO ip_bans (ip, until, reason, by_uid) VALUES (?, ?, ?, ?)" };
            const std::string ip = moderation::ip_string(target->address);
            const std::string ip_reason = clean(target_name + ": " + reason, 128);
            MYSQL_BIND params[4] = { make_bind_in(ip), make_bind_in(until), make_bind_in(ip_reason), make_bind_in(pPeer->user_id) };
            hStmt.bind_param(params);
            hStmt.execute();
        }

        on::ConsoleMessage(target, std::format("`4You were banned for {}.``{}", moderation::duration(minutes < 0 ? -1 : minutes * 60), reason.empty() ? "" : " Reason: " + reason));
        send_gameMessage(target, {.message = "action|logon_fail\n"});
        enet_peer_disconnect_later(target, 0);
    }
    else
    {
        ::peer offline{};
        if (!offline.exists(target_name)) return reply(event, std::format("`4No account called {}.``", target_name));
        offline.growid = target_name;
        if (!outranks(event, *pPeer, static_cast<u_char>(offline.mysql_select<signed>("role")))) return;
        offline.mysql_update("banned_until", until);
        offline.mysql_update("ban_reason", reason);
    }
    moderation::log(*pPeer, "ban", target_name, std::format("{} {}", list[1], reason));
    reply(event, std::format("`2Banned {} for {}.``", target_name, moderation::duration(minutes < 0 ? -1 : minutes * 60)));
}

void unban(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const std::vector<std::string> list = words(text);
    if (list.empty()) return reply(event, "Usage: /unban <`$name``>");

    ::peer offline{};
    if (!offline.exists(list[0])) return reply(event, std::format("`4No account called {}.``", list[0]));
    offline.growid = list[0];
    offline.mysql_update("banned_until", 0ll);
    offline.mysql_update("ban_reason", std::string{});

    /* @note IP bans name the account at the start of their reason */
    ::hStmt hStmt{ "DELETE FROM ip_bans WHERE reason LIKE CONCAT(?, ':%')" };
    MYSQL_BIND param = make_bind_in(list[0]);
    hStmt.bind_param(&param);
    hStmt.execute();

    moderation::log(*pPeer, "unban", list[0], "");
    reply(event, std::format("`2Unbanned {}.``", list[0]));
}

void mute(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const std::vector<std::string> list = words(text);
    if (list.size() < 2ull) return reply(event, "Usage: /mute <`$name``> <`$minutes``, `$2h`` or `$7d``> [reason]");

    const long long minutes = read_minutes(list[1]);
    if (minutes <= 0) return reply(event, "`4The time must look like 30, 2h or 7d.``");
    const long long until = std::time(nullptr) + minutes * 60;
    const std::string reason = clean(rest(list, 2), 100);

    ENetPeer *target = moderation::find_online(list[0]);
    std::string target_name = list[0];
    if (target != nullptr)
    {
        ::peer *pTarget = static_cast<::peer*>(target->data);
        if (target == event.peer || !outranks(event, *pPeer, pTarget->role)) return;
        target_name = pTarget->growid;
        pTarget->muted_until = until;
        pTarget->mysql_update("muted_until", until);
        on::ConsoleMessage(target, std::format("`4You were muted for {}.``{}", moderation::duration(minutes * 60), reason.empty() ? "" : " Reason: " + reason));
    }
    else
    {
        ::peer offline{};
        if (!offline.exists(target_name)) return reply(event, std::format("`4No account called {}.``", target_name));
        offline.growid = target_name;
        if (!outranks(event, *pPeer, static_cast<u_char>(offline.mysql_select<signed>("role")))) return;
        offline.mysql_update("muted_until", until);
    }
    moderation::log(*pPeer, "mute", target_name, std::format("{} {}", list[1], reason));
    reply(event, std::format("`2Muted {} for {}.``", target_name, moderation::duration(minutes * 60)));
}

void unmute(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const std::vector<std::string> list = words(text);
    if (list.empty()) return reply(event, "Usage: /unmute <`$name``>");

    if (ENetPeer *target = moderation::find_online(list[0]); target != nullptr)
    {
        static_cast<::peer*>(target->data)->muted_until = 0;
        on::ConsoleMessage(target, "`2You can talk again.``");
    }
    ::peer offline{};
    if (!offline.exists(list[0])) return reply(event, std::format("`4No account called {}.``", list[0]));
    offline.growid = list[0];
    offline.mysql_update("muted_until", 0ll);

    moderation::log(*pPeer, "unmute", list[0], "");
    reply(event, std::format("`2Unmuted {}.``", list[0]));
}

void pull(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const std::vector<std::string> list = words(text);
    if (list.empty()) return reply(event, "Usage: /pull <`$name``> - brings them to your world");
    if (pPeer->netid == 0) return;

    ENetPeer *target = moderation::find_online(list[0]);
    if (target == nullptr || target == event.peer) return reply(event, std::format("`4{}`` is not online.", list[0]));
    ::peer *pTarget = static_cast<::peer*>(target->data);

    const std::string world_name = pPeer->recent_worlds.back();
    ENetEvent target_event{};
    target_event.peer = target;
    on::ConsoleMessage(target, std::format("`5You were pulled by {}.``", pPeer->display_growid));
    action::join_request(target_event, "", world_name);

    moderation::log(*pPeer, "pull", pTarget->growid, world_name);
}

void warpto(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const std::vector<std::string> list = words(text);
    if (list.empty()) return reply(event, "Usage: /warpto <`$name``> - go to the world they're in");

    ENetPeer *target = moderation::find_online(list[0]);
    if (target == nullptr || target == event.peer) return reply(event, std::format("`4{}`` is not online.", list[0]));
    const ::peer *pTarget = static_cast<::peer*>(target->data);
    if (pTarget->netid == 0) return reply(event, std::format("`4{}`` is not in a world.", pTarget->growid));

    action::join_request(event, "", pTarget->recent_worlds.back());
}

void broadcast(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const std::string message = clean(std::string{ command_arg(text) }, 200);
    if (message.empty()) return reply(event, "Usage: /broadcast <`$message``> - everyone online sees it");

    peers("", PeerSearch::EVERYONE, [&](ENetPeer *peer)
    {
        const ::peer *pOthers = static_cast<::peer*>(peer->data);
        if (pOthers == nullptr || !pOthers->authenticated) return;
        on::ConsoleMessage(peer, std::format("CP:0_PL:0_OID:_CT:[SB]_ `#** Staff broadcast from {}``: `${}``", pPeer->growid, message));
        send_varlist(peer, { "OnTextOverlay", std::format("`#{}``", message) });
        play_sfx(peer, "audio/beep.wav");
    });
    moderation::log(*pPeer, "broadcast", "", message);
}

void give(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const std::vector<std::string> list = words(text);
    if (list.size() < 2ull) return reply(event, "Usage: /give <`$name``> <`$item id``> [count, up to 200] or /give <`$name``> gems <`$amount``>");

    ENetPeer *target = moderation::find_online(list[0]);
    if (target == nullptr) return reply(event, std::format("`4{}`` is not online.", list[0]));
    ::peer *pTarget = static_cast<::peer*>(target->data);
    ENetEvent target_event{};
    target_event.peer = target;

    if (list[1] == "gems")
    {
        const long long amount = (list.size() > 2ull) ? atoll(list[2].c_str()) : 0;
        if (amount == 0 || amount > 1'000'000'000ll || amount < -1'000'000'000ll) return reply(event, "`4Give between 1 and 1,000,000,000 gems (negative takes them away).``");
        pTarget->gems = std::max(0ll, pTarget->gems + amount);
        on::SetBux(target_event);
        moderation::log(*pPeer, "give", pTarget->growid, std::format("{} gems", amount));
        return reply(event, std::format("`2Gave {} gems to {}.``", amount, pTarget->growid));
    }

    const int id = atoi(list[1].c_str());
    if (id <= 0 || static_cast<std::size_t>(id) >= items.size()) return reply(event, "`4That item doesn't exist.``");
    const int count = std::clamp((list.size() > 2ull) ? atoi(list[2].c_str()) : 1, 1, 200);

    const u_short left = modify_item_inventory(target_event, ::slot(static_cast<short>(id), static_cast<short>(count)));
    on::ConsoleMessage(target, std::format("`2You got {} `w{}`` from {}.``", count - left, id_to_item(id).raw_name, pPeer->growid));
    moderation::log(*pPeer, "give", pTarget->growid, std::format("{} x{}", id, count - left));
    reply(event, std::format("`2Gave {} `w{}`` to {}.``", count - left, id_to_item(id).raw_name, pTarget->growid));
}

void setrole(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const std::vector<std::string> list = words(text);
    if (list.size() < 2ull) return reply(event, "Usage: /setrole <`$name``> <`$player``, `$mod`` or `$dev``>");

    const std::string wanted = lower(list[1]);
    const int role = (wanted == "player" || wanted == "0") ? PLAYER :
                     (wanted == "mod" || wanted == "moderator" || wanted == "1") ? MODERATOR :
                     (wanted == "dev" || wanted == "developer" || wanted == "2") ? DEVELOPER : -1;
    if (role < 0) return reply(event, "`4The role must be player, mod or dev.``");

    ENetPeer *target = moderation::find_online(list[0]);
    std::string target_name = list[0];
    if (target != nullptr)
    {
        ::peer *pTarget = static_cast<::peer*>(target->data);
        if (target == event.peer) return reply(event, "`4You can't change your own role.``");
        target_name = pTarget->growid;
        pTarget->role = static_cast<u_char>(role);
        pTarget->mysql_update("role", role);
        on::ConsoleMessage(target, std::format("`2Your role is now {}. Rejoin the world to see your new name colour.``", list[1]));
    }
    else
    {
        ::peer offline{};
        if (!offline.exists(target_name)) return reply(event, std::format("`4No account called {}.``", target_name));
        offline.growid = target_name;
        offline.mysql_update("role", role);
    }
    moderation::log(*pPeer, "setrole", target_name, list[1]);
    reply(event, std::format("`2{} is now {}.``", target_name, list[1]));
}

void givegems(ENetEvent& event, const std::string_view text)
{
    const std::vector<std::string> list = words(text);
    if (list.size() < 2ull) return reply(event, "Usage: /givegems <`$name``> <`$amount``>");
    give(event, std::format("/give {} gems {}", list[0], list[1]));
}

void trashall(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    int trashed{};
    const std::vector<::slot> slots = pPeer->slots; // @note copy: modify_item_inventory changes the list
    for (const ::slot &slot : slots)
    {
        if (slot.count <= 0 || slot.id == 18/*Fist*/ || slot.id == 32/*Wrench*/) continue;
        modify_item_inventory(event, ::slot(slot.id, static_cast<short>(-slot.count)));
        ++trashed;
    }
    std::erase_if(pPeer->slots, [](const ::slot &slot) { return slot.count <= 0; });
    on::SetClothing(event.peer);
    if (trashed > 0) play_sfx(event.peer, "audio/trash.wav");
    reply(event, std::format("`2Trashed {} kinds of items. Only your Fist and Wrench are left.``", trashed));
}

void playermode(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    if (!pPeer->player_mode)
    {
        pPeer->real_role = pPeer->role;
        pPeer->role = PLAYER;
        pPeer->player_mode = true;
        reply(event, "`2Player mode is ON.`` You're treated like a normal player: no staff commands, and your worlds, locks and machines don't know you're the owner. Type `$/playermode`` again to get your powers back (they also come back when you log in again).");
    }
    else
    {
        pPeer->role = pPeer->real_role;
        pPeer->player_mode = false;
        reply(event, "`2Player mode is OFF.`` Your owner and staff powers are back.");
    }
}

void mail(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const std::vector<std::string> list = words(text);
    if (list.size() < 3ull) return reply(event, "Usage: /mail <`$name``> <`$item id`` or `$gems``> <`$count``> [message]  (count 0 sends just the message)");

    std::string target_name = list[0];
    int uid{};
    if (ENetPeer *target = moderation::find_online(list[0]); target != nullptr)
    {
        const ::peer *pTarget = static_cast<::peer*>(target->data);
        uid = pTarget->user_id;
        target_name = pTarget->growid;
    }
    else
    {
        ::peer offline{};
        if (!offline.exists(target_name)) return reply(event, std::format("`4No account called {}.``", target_name));
        offline.growid = target_name;
        uid = offline.mysql_select<signed>("uid");
    }

    const int id = (lower(list[1]) == "gems") ? MAIL_GEMS : atoi(list[1].c_str());
    if (id <= 0 || static_cast<std::size_t>(id) >= items.size()) return reply(event, "`4That item doesn't exist.``");
    const int count = std::clamp(atoi(list[2].c_str()), 0, (id == MAIL_GEMS) ? 1'000'000'000 : 10'000);

    std::vector<std::pair<int, int>> rewards{};
    if (count > 0) rewards.emplace_back(id, count);
    const std::string message = (list.size() > 3ull) ? rest(list, 3) : std::format("A gift from {}.", pPeer->growid);

    mail_send(uid, std::format("Mail from {}", pPeer->growid), message, rewards, (count > 0) ? id : 0);
    moderation::log(*pPeer, "mail", target_name, (count > 0) ? std::format("{} x{}", id, count) : message.substr(0, 200));
    reply(event, std::format("`2Mail sent to {}.``", target_name));
}

void maillayout(ENetEvent& event, const std::string_view text)
{
    const std::vector<std::string> list = words(text);
    if (list.empty())
        return reply(event, std::format("Mail layout: `w{}``. Usage: /maillayout <six names from id, status, icon, expires, left or a number, comma separated>. Default: id,icon,icon,status,expires,status", mail_layout));
    if (readch(list[0], ',').size() != 6ull) return reply(event, "`4Give exactly six names, comma separated.``");
    mail_layout = list[0];
    reply(event, std::format("`2Mail layout is now {}.`` Close and open your mailbox to see it.", mail_layout));
}

void mailrewards(ENetEvent& event, const std::string_view text)
{
    const std::vector<std::string> list = words(text);
    const int format = list.empty() ? 0 : atoi(list[0].c_str());
    if (format < 1 || format > 8)
        return reply(event, std::format("Mail reward format: `w{}``. Usage: /mailrewards <1-8>", mail_rewards));
    mail_rewards = format;
    reply(event, std::format("`2Mail reward format is now {}.`` Close and open your mailbox to see it.", format));
}
