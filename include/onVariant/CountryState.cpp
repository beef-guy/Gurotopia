#include "pch.hpp"

#include "database/titles.hpp"

#include "CountryState.hpp"

void on::CountryState(ENetEvent& event) 
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (pPeer->netid == 0 || pPeer->recent_worlds.empty()) return;

    /* @todo add |showGuild when we add guild system; also add peer's real country */
    const std::string state = titles::country_state(*pPeer); // @note "us|maxLevel|doctor": the badges of the titles they show
    const int netid = pPeer->netid;
    peers(pPeer->recent_worlds.back(), PeerSearch::WORLD, [&state, netid](ENetPeer *peer)
    {
        send_varlist(peer, { "OnCountryState", state }, netid);
    });
}