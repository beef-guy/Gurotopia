#include "pch.hpp"

#include "onVariant/SetClothing.hpp"
#include "commands/punch.hpp"
#include "onVariant/ConsoleMessage.hpp"

#include "item_activate.hpp"

void item_activate(ENetEvent& event, ::gamePacket gamePacket)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    const ::item &item = id_to_item(gamePacket.id);
    if (pPeer->have_slot(::slot(gamePacket.id, 0)) == 0) return; // @note can only use items peer has

    if (item.cloth_type < pPeer->clothing.size())
    {
        float &current_cloth = pPeer->clothing[item.cloth_type]; // @note ID of the current clothing being changed

        current_cloth = (current_cloth == gamePacket.id) ? 0 : gamePacket.id;

        pPeer->update_effects();
        
        /* @note this is so we can add the latest punch effect (if any) */
        u_short punch_id = get_punch_id(static_cast<u_int>(current_cloth));
        if (punch_id != 0)
            pPeer->punch_effect = punch_id;

        send_varlist(event.peer, { "OnEquipNewItem", gamePacket.id }, pPeer->netid);
        on::SetClothing(event.peer); // @todo
    }
    else 
    {
        /* @note copy these, giving an item can move the backpack and break an iterator into it */
        const short id = gamePacket.id;
        const short count = pPeer->have_slot(::slot(id, 0));

        if (id == 242 && count >= 100)  
        {
            const u_short nokori = modify_item_inventory(event, {1796, 1});
            if (nokori == 0) 
            {
                modify_item_inventory(event, ::slot(id, -100));
                const std::string compressed = "You compressed 100 `2World Lock`` into a `2Diamond Lock``!";
                send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, compressed, 0u, 1u });
                on::ConsoleMessage(event.peer, compressed);
            }
        }
        else if (id == 1796 && count >= 1)
        {
            const u_short nokori = modify_item_inventory(event, {242, 100});
            short hyaku = 100 - nokori;
            if (hyaku == 100) 
            {
                modify_item_inventory(event, ::slot(id, -1));
                const std::string shattered = "You shattered a `2Diamond Lock`` into 100 `2World Lock``!";
                send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, shattered, 0u, 1u });
                on::ConsoleMessage(event.peer, shattered);
            }
            else modify_item_inventory(event, ::slot(242, -hyaku)); // @note return wls if can't hold 100
        }
        else if (id == 1486 && count >= 100)  
        {
            const u_short nokori = modify_item_inventory(event, {6802, 1});
            if (nokori == 0)
                modify_item_inventory(event, ::slot(id, -100));
        }
        else if (id == 6802 && count >= 1)
        {
            const u_short nokori = modify_item_inventory(event, {1486, 100});
            short hyaku = 100 - nokori;
            if (hyaku == 100)
                modify_item_inventory(event, ::slot(id, -1));
            else 
                modify_item_inventory(event, ::slot(1486, -hyaku)); // @note return growtoken if can't hold 100
        }
    }
}
