#include "pch.hpp"

#include "onVariant/ConsoleMessage.hpp"
#include "tools/sound.hpp"
#include "onVariant/SetBux.hpp"

#include "trash_item.hpp"

void trash_item(ENetEvent& event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    const short itemID = atoi(hPipe["itemID"].c_str());
    const int owned = pPeer->have_slot(::slot(itemID, 0));
    const short count = static_cast<short>(std::min(atoi(hPipe["count"].c_str()), owned));
    if (count <= 0) return; // @note also covers items peer doesn't have

    const ::item &item = id_to_item(itemID);

    /* @note recycling pays gems by rarity: rarity / 50 per item (rounded down over the whole stack). untradeable and
     * special (rarity 999) items pay nothing. @todo the real game's exact rate isn't known, this is an estimate */
    const bool pays = !(item.cat & CAT_UNTRADEABLE) && item.rarity > 0 && item.rarity < 999;
    const int gems = pays ? (count * item.rarity) / 50 : 0;

    modify_item_inventory(event, ::slot(itemID, -count));
    if (gems > 0)
    {
        pPeer->gems += gems;
        on::SetBux(event);
    }
    play_sfx(event.peer, "audio/trash.wav");
    on::ConsoleMessage(event.peer, std::format("{} `w{}`` recycled, `w{}`` gems earned.", count, item.raw_name, gems));
}