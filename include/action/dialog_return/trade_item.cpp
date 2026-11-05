#include "pch.hpp"

#include "action/trade.hpp"

#include "trade_item.hpp"

#include <algorithm>

void trade_item(ENetEvent& event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (pPeer->trade_with == 0) return; // @note not trading

    const short itemID = atoi(hPipe["itemID"].c_str());
    const int count  = atoi(hPipe["count"].c_str());

    if (id_to_item(itemID).cat & CAT_UNTRADEABLE) return;
    if (count <= 0 || count > pPeer->have_slot(::slot(itemID, 0))) return;

    if (auto it = std::ranges::find(pPeer->trade_offers, itemID, &::slot::id); it != pPeer->trade_offers.end())
        it->count = static_cast<short>(count); // @note changing the amount of something already offered
    else if (pPeer->trade_offers.size() >= 4)
    {
        send_varlist(event.peer, { "OnTextOverlay", "You can only offer 4 different items." });
        return;
    }
    else pPeer->trade_offers.emplace_back(::slot{itemID, static_cast<short>(count)});

    trade::changed(event);
}
