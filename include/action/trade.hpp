#pragma once

/* @note trading between two players in the same world */
namespace trade
{
    /* @return the peer this peer is trading with, only if both sides opened the trade. nullptr otherwise */
    extern ENetPeer *partner(const ::peer &peer);

    /* @note A wrenched B and pressed Trade */
    extern void request(ENetEvent &event, ENetPeer *other);

    /* @note the offer changed: unlock both sides and show both offers to both players */
    extern void changed(ENetEvent &event);

    /* @note end this peer's trade (and the partner's). message goes to the partner */
    extern void cancel(ENetPeer *peer, const std::string &message = "");
}

namespace action
{
    extern void trade_accept(ENetEvent& event, const std::string& header);
    extern void trade_cancel(ENetEvent& event, const std::string& header);
}

extern void trade_confirm(ENetEvent& event, const ::hPipe &hPipe);
