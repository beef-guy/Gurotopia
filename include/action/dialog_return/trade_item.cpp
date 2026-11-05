#include "pch.hpp"

#include "onVariant/TradeStatus.hpp"

#include "trade_item.hpp"

void trade_item(ENetEvent& event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    short itemID = atoi(hPipe["itemID"].c_str());
    short count  = atoi(hPipe["count"].c_str());

    if (pPeer->trade_offers.size() >= 4) return; // @todo get the prompt message
    if (count <= 0 || count > pPeer->have_slot(::slot(itemID, 0))) return;

    pPeer->trade_offers.emplace_back(::slot{itemID, count});

    on::TradeStatus(event.peer);

    peers(pPeer->recent_worlds.back(), PeerSearch::WORLD, [&event, &pPeer](ENetPeer *peer) 
    {
        ::peer *pOthers = static_cast<::peer*>(peer->data);

        if (pOthers->user_id == /*@todo trading with*/0 ||
            pOthers->user_id == pPeer->user_id)
        {
            send_varlist(event.peer, { "OnTextOverlay", "The deal has changed" });
            // action|play_sfx
            // audio/tile_removed.wav
            // delayMS|0
        }
    });
}