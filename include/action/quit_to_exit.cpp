#include "pch.hpp"

#include "onVariant/RequestWorldSelectMenu.hpp"
#include "onVariant/ConsoleMessage.hpp"

#include "quit_to_exit.hpp"
#include "trade.hpp"
#include "action/dialog_return/wrenchables.hpp"
#include "action/special_items.hpp"

void action::quit_to_exit(ENetEvent& event, const std::string& header, bool skip_selection = false) 
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (pPeer == nullptr || pPeer->netid == 0) return; // @note peer was not in a world, therefore nothing to exit from.

    trade::cancel(event.peer, std::format("`4{} left, so the trade was canceled.``", pPeer->display_growid));
    magplant_take_remote(event); // @note the MAGPLANT 5000 Remote only works in the world it came from

    auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
    if (world == worlds.end()) return;
    special_left_world(event, *world); // @note leave a running game, stop a challenge

    std::string message = std::format("`5<{} left, `w{}`` others here>``", pPeer->display_growid, world->visitors-1);
    std::string netid = std::format("netID|{}\n", pPeer->netid);
    std::string pId = std::format("pId|{}\n", pPeer->user_id); // @note this is found during OnSpawn 'eid', the value is the same for user_id.
    peers(pPeer->recent_worlds.back(), PeerSearch::WORLD, [&event, pPeer, message, netid, pId](ENetPeer *peer) 
    {
        ::peer *pOthers = static_cast<::peer*>(peer->data);
        if (pOthers->user_id == pPeer->user_id) return;

        on::ConsoleMessage(peer, message);
        send_varlist(peer, { "OnRemove", netid, pId }); // @todo
    });

    if (world->visitors <= 1) // @note last one out, delete memory copy of world.
    {
        world->save(); // @note erase() overwrites this world without running ~world(), so save it here.
        worlds.erase(world);
    }
    else --world->visitors;
    pPeer->netid = 0; // this will fix any packets being sent outside of world; this can also be used to check if peer is not in a world.

    pPeer->display_growid = std::format("`w{}``", pPeer->growid);
    if (!skip_selection) on::RequestWorldSelectMenu(event);
}