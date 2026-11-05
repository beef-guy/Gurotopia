#include "pch.hpp"

#include "Action.hpp"

void on::Action(ENetEvent& event, const std::string_view text) 
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    std::string_view name = text.substr(0, text.find(' ')); // @note "/wave hi" -> "/wave"
    if (name.starts_with('/')) name.remove_prefix(1);

    std::string_view to_slang =
        (name == "facepalm") ? "fp" :
        (name == "shrug") ? "idk" :
        (name == "foldarms") ? "fold" :
        (name == "fa") ? "fold" :
        (name == "stubborn") ? "fold" : name;

    const std::string &action = ('/' + std::string(to_slang));
    peers(pPeer->recent_worlds.back(), PeerSearch::WORLD, [&pPeer, &action](ENetPeer *peer)
    {
        send_varlist(peer, { "OnAction", action }, pPeer->netid);
    });
}
