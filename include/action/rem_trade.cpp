#include "pch.hpp"

#include "action/trade.hpp"

#include "rem_trade.hpp"

void action::rem_trade(ENetEvent& event, const std::string& header) 
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (pPeer->trade_with == 0) return;

    const std::vector<std::string> pipes = readch(header, '|');
    if (pipes.size() < 4ull) return;

    const int id = atoi(pipes[3ull].c_str());
    std::erase_if(pPeer->trade_offers, [id](const ::slot &slot) { return slot.id == id; });
    trade::changed(event);
}
