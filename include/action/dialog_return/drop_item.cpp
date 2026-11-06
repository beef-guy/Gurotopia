#include "pch.hpp"

#include "drop_item.hpp"
#include "action/ghosts.hpp"

void drop_item(ENetEvent& event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
    if (world == worlds.end()) return;

    if (pPeer->netid == 0) return;

    const short itemID = atoi(hPipe["itemID"].c_str());
    const int owned = pPeer->have_slot(::slot(itemID, 0));
    const short count = static_cast<short>(std::min(atoi(hPipe["count"].c_str()), owned));
    if (count <= 0) return; // @note also covers items peer doesn't have
    if (id_to_item(itemID).cat & CAT_UNTRADEABLE) return;

    float x_nabor = (pPeer->facing_left) ? pPeer->pos.x - 32 : pPeer->pos.x + 32; // @note peer's naboring tile (drop position)

    /* @note Ghosts-In-A-Jar dropped in front of a Spirit Storage Unit go into it */
    const int stored = ssu_take_jars(event, *world, ::pos{x_nabor + 16.0f, pPeer->pos.y + 16.0f}.by_32(true), itemID, count);
    modify_item_inventory(event, ::slot(itemID, -count));
    if (stored >= count) return;
    add_drop(event, ::slot(itemID, static_cast<short>(count - stored)), {x_nabor, pPeer->pos.y}, *world);
}