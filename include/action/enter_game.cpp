#include "pch.hpp"
#include "action/dialog_return/wrenchables.hpp"

#include "onVariant/RequestWorldSelectMenu.hpp"
#include "onVariant/RequestGazette.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "onVariant/SetBux.hpp"
#include "automate/holiday.hpp"
#include "database/friends.hpp"
#include "database/titles.hpp"

#include "action/event_buttons.hpp"

#include "enter_game.hpp"

void action::enter_game(ENetEvent& event, const std::string& header) 
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    pPeer->display_growid = std::format("`w{}``", pPeer->growid);
    titles::load_peer(*pPeer); // @note which titles they were given, and which they switched off
    /* @note load friends, then tell the ones online that we're here */
    pPeer->friends.clear();
    for (const ::friend_row &row : friends_of(pPeer->user_id)) pPeer->friends.emplace_back(row.uid);
    const std::size_t online = friends_online(*pPeer);
    on::ConsoleMessage(event.peer, 
        std::format("Welcome back, {}. {}", 
            pPeer->display_growid,
            (online == 0) ? "No friends are online." : 
            (online == 1) ? "`w1`` friend is online." : std::format("`w{}`` friends are online.", online)
        )
    );
    friends_alert(event.peer, true);
    daily_challenge_settle(); // @note mail yesterday's Daily Challenge prizes, if nobody has yet
    heart_monitors_refresh(static_cast<::peer*>(event.peer->data)->user_id); // @note their Heart Monitors start beating
    on::ConsoleMessage(event.peer, holiday_greeting().second);
    if (active_holiday == holiday::HALLOWEEN)
    {
        on::ConsoleMessage(event.peer, "Growtopians have collectively released `20`` `9Corrupted Souls`` from the Tomb of Growganoth! Growganoth is pleased and unleashes the corrupted `2Tarantula Legs`` upon Growtopia!");
        send_varlist(event.peer, { "OnEventButtonDataSet", "HalloweenButton", 1, /*@todo*/ });
    }
    on::ConsoleMessage(event.peer, "`3Today is (null)!`` Your first (null) quest will give 1 `2Growtoken`` and all (null) quests will give 25% bonus points!");
    on::ConsoleMessage(event.peer, "`5Personal Settings active:`` `#Punch Joystick enabled``");
    
    if (const int remotes = pPeer->have_slot(::slot(5640, 0)); remotes > 0) pPeer->emplace(::slot(5640, static_cast<short>(-remotes))); // @note a MAGPLANT 5000 Remote left over from a crash
    send_inventory_state(event);

    on::SetBux(event);
    send_varlist(event.peer, { "OnSetVouchers", 0 });
    send_varlist(event.peer, { "SetHasGrowID", 1u, pPeer->growid.c_str(), "" });
    {
        std::tm time = localtime();

        send_varlist(event.peer, {
            "OnTodaysDate",
            time.tm_mon + 1,
            time.tm_mday,
            0u, // @todo
            0u // @todo
        });
    } // @note delete time

    send_varlist(event.peer, { "OnRequestHoustonPricing" });

    on::RequestWorldSelectMenu(event);
    on::RequestGazette(event);

    send_data(*event.peer, compress_state(::gamePacket{ .type = 0x16 /*PACKET_PING_REQUEST*/ }));
    if (pPeer->game_version >= 5.47)
    {
        send_varlist(event.peer, {
            "OnSetFeatureEnableFlags",
            "EAcGAQoDEAQRBQ0JCAwCDw4="
        });
    }
}
