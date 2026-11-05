#include "pch.hpp"

#include "onVariant/TradeStatus.hpp"

#include "rem_trade.hpp"

void action::rem_trade(ENetEvent& event, const std::string& header) 
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    const std::vector<std::string> pipes = readch(header, '|');
    if (pipes.size() < 4ull) return;

    for (auto it = pPeer->trade_offers.begin(); it != pPeer->trade_offers.end(); ++it)
    {
        if (it->id == atoi(pipes[3ull].c_str()))
        {
            pPeer->trade_offers.erase(it);
            break;
        }
    }
    on::TradeStatus(event.peer);
}