#include "pch.hpp"

#include "onVariant/ConsoleMessage.hpp"

#include "trash_item.hpp"

void trash_item(ENetEvent& event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    const short itemID = atoi(hPipe["itemID"].c_str());
    const int owned = pPeer->have_slot(::slot(itemID, 0));
    const short count = static_cast<short>(std::min(atoi(hPipe["count"].c_str()), owned));
    if (count <= 0) return; // @note also covers items peer doesn't have

    const ::item &item = id_to_item(itemID);

    modify_item_inventory(event, ::slot(itemID, -count));
    on::ConsoleMessage(event.peer, std::format("{} `w{}`` recycled, `w0`` gems earned.", count, item.raw_name));
}