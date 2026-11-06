#include "pch.hpp"

#include "database/friends.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "action/friends.hpp"
#include "commands/warp.hpp"
#include "tools/create_dialog.hpp"

#include <algorithm>

#include "friends.hpp"

static void show_friend(ENetEvent& event, int uid)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    const std::vector<::friend_row> rows = friends_of(pPeer->user_id);
    const auto row = std::ranges::find(rows, uid, &::friend_row::uid);
    if (row == rows.end()) return show_friends(event, true); // @note not friends anymore

    ENetPeer *other = online_by_uid(uid);
    const ::peer *pOthers = (other != nullptr) ? static_cast<::peer*>(other->data) : nullptr;
    const bool in_world = pOthers != nullptr && pOthers->netid != 0 && !pOthers->recent_worlds.back().empty();

    ::create_dialog dialog = ::create_dialog()
        .set_default_color("`o")
        .add_label_with_icon("big", std::format("`w{}``", row->growid), 1366)
        .embed_data("friendID", uid)
        .add_spacer("small")
        .add_textbox(
            (pOthers == nullptr) ? std::format("{} is `4offline``.", row->growid) :
            (in_world) ? std::format("{} is `2online`` in `5{}``.", row->growid, pOthers->recent_worlds.back()) :
                         std::format("{} is `2online`` but not in a world.", row->growid))
        .add_spacer("small");

    if (in_world && pOthers->recent_worlds.back() != pPeer->recent_worlds.back())
        dialog.add_button("warp", std::format("`wWarp to `5{}````", pOthers->recent_worlds.back()));
    dialog.add_button("remove", "`4Remove as friend``")
          .add_button("back", "Back")
          .add_quick_exit();

    send_varlist(event.peer, { "OnDialogRequest", dialog.end_dialog("friend_edit", "Close", "") });
}

void friends(ENetEvent& event, const ::hPipe &hPipe)
{
    const std::string &button = hPipe["buttonClicked"];

    if (button == "friend_all") show_friends(event, true);
    else if (button == "friend_online") show_friends(event, false);
    else if (button == "back") action::friends(event, "");
    else if (button.starts_with("friend_"))
    {
        const int uid = atoi(button.substr(7).c_str());
        if (uid != 0) show_friend(event, uid);
    }
}

void friend_edit(ENetEvent& event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const std::string &button = hPipe["buttonClicked"];
    const int uid = atoi(hPipe["friendID"].c_str());
    if (uid == 0) return;

    const std::vector<::friend_row> rows = friends_of(pPeer->user_id);
    const auto row = std::ranges::find(rows, uid, &::friend_row::uid);

    if (button == "back") show_friends(event, true);
    else if (row == rows.end()) return; // @note only friends from here on
    else if (button == "remove")
    {
        friends_remove(pPeer->user_id, uid);
        std::erase(pPeer->friends, uid);
        on::ConsoleMessage(event.peer, std::format("`3FRIEND REMOVED:`` `o{} is no longer your friend.``", row->growid));

        if (ENetPeer *other = online_by_uid(uid))
            std::erase(static_cast<::peer*>(other->data)->friends, pPeer->user_id);

        show_friends(event, true);
    }
    else if (button == "warp")
    {
        ENetPeer *other = online_by_uid(uid);
        if (other == nullptr) return;
        const ::peer *pOthers = static_cast<::peer*>(other->data);
        if (pOthers->netid == 0 || pOthers->recent_worlds.back().empty()) return;

        warp(event, std::format("/warp {}", pOthers->recent_worlds.back())); // @note same checks as /warp (locked, level, banned)
    }
}

void friend_add(ENetEvent& event, int netid)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (!pPeer->authenticated || pPeer->user_id == 0 || pPeer->netid == 0) return;

    ENetPeer *other = nullptr;
    peers(pPeer->recent_worlds.back(), PeerSearch::WORLD, [&](ENetPeer *p)
    {
        const ::peer *pOthers = static_cast<::peer*>(p->data);
        if (other == nullptr && pOthers->netid == netid && p != event.peer) other = p;
    });
    if (other == nullptr) return;
    ::peer *pOthers = static_cast<::peer*>(other->data);
    if (!pOthers->authenticated || pOthers->user_id == 0 || pOthers->user_id == pPeer->user_id) return;

    if ((std::ranges::find(pPeer->friends, pOthers->user_id) != pPeer->friends.end()))
    {
        on::ConsoleMessage(event.peer, std::format("`oYou're already friends with {}.``", pOthers->growid));
        return;
    }
    if (pPeer->friends.size() >= MAX_FRIENDS || pOthers->friends.size() >= MAX_FRIENDS)
    {
        on::ConsoleMessage(event.peer, std::format("`4Oops:`` `oFriend lists are limited to {} friends.``", MAX_FRIENDS));
        return;
    }

    /* @note they already asked us, so this accepts */
    if ((std::ranges::find(pPeer->friend_requests, pOthers->user_id) != pPeer->friend_requests.end()))
    {
        friends_add(pPeer->user_id, pOthers->user_id);
        std::erase(pPeer->friend_requests, pOthers->user_id);
        std::erase(pOthers->friend_requests, pPeer->user_id);
        pPeer->friends.emplace_back(pOthers->user_id);
        pOthers->friends.emplace_back(pPeer->user_id);

        on::ConsoleMessage(event.peer, std::format("`3FRIEND ADDED:`` `oYou're now friends with `w{}``!``", pOthers->growid));
        on::ConsoleMessage(other,      std::format("`3FRIEND ADDED:`` `oYou're now friends with `w{}``!``", pPeer->growid));
        return;
    }

    if ((std::ranges::find(pOthers->friend_requests, pPeer->user_id) != pOthers->friend_requests.end()))
    {
        on::ConsoleMessage(event.peer, std::format("`oYou already sent {} a friend request.``", pOthers->growid));
        return;
    }
    pOthers->friend_requests.emplace_back(pPeer->user_id);
    on::ConsoleMessage(event.peer, std::format("`3FRIEND REQUEST SENT:`` `oYou sent a friend request to `w{}``.``", pOthers->growid));
    on::ConsoleMessage(other, std::format("`3FRIEND REQUEST:`` `oYou've received a friend request from `w{}``! To accept, simply wrench them and choose `2Add as friend``.``", pPeer->growid));
}
