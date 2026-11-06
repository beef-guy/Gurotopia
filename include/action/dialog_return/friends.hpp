#pragma once

/* @note the friends list ("friends") and one friend's menu ("friend_edit") */
extern void friends(ENetEvent& event, const ::hPipe &hPipe);
extern void friend_edit(ENetEvent& event, const ::hPipe &hPipe);

/* @note "Add as friend" from the wrench menu. `netid` is the other player's */
extern void friend_add(ENetEvent& event, int netid);
