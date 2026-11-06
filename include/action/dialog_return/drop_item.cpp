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

    modify_item_inventory(event, ::slot(itemID, -count));
    add_drop(event, ::slot(itemID, count), {x_nabor, pPeer->pos.y}, *world);

    /* @note Ghosts-In-A-Jar dropped in front of a Spirit Storage Unit wait there until steam powers the unit */
    if (itemID == GHOST_IN_A_JAR || itemID == MIND_GHOST_IN_A_JAR)
    {
        const ::pos front = ::pos{x_nabor + 16.0f, pPeer->pos.y + 16.0f}.by_32(true);
        for (const int dx : {0, -1, 1})
        {
            const ::pos at{ front.x + dx, front.y };
            if (valid_tile(*world, at) && world->blocks[cord(at.x_int(), at.y_int())].fg == SPIRIT_STORAGE_UNIT)
            {
                send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, "Now send `2Steam`` into the `2Spirit Storage Unit`` to store the ghost!", 0u, 1u });
                break;
            }
        }
    }
}