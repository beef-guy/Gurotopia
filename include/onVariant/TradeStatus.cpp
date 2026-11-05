#include "pch.hpp"

#include "TradeStatus.hpp"

void on::TradeStatus(ENetPeer *to, const ::peer &owner) 
{
    std::string add_slots;
    for (const ::slot &slot : owner.trade_offers)
    {
        add_slots += std::format("add_slot|{}|{}\n", slot.id, slot.count);
    }
    send_varlist(to, {
        "OnTradeStatus",
        owner.netid,
        "",
        std::format("`o{}'s offer.``", owner.display_growid),
        std::format(
            "{}"
            "locked|0\n"
            "reset_locks|1\n"
            "accepted|{}\n", 
            add_slots, owner.trade_accepted ? 1 : 0)
        }
    );
}
