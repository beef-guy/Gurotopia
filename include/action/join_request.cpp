#include "pch.hpp"
#include "action/event_buttons.hpp"

#include "onVariant/EmoticonDataChanged.hpp"
#include "onVariant/Spawn.hpp"
#include "onVariant/BillboardChange.hpp"
#include "onVariant/SetClothing.hpp"
#include "onVariant/CountryState.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "onVariant/TradeStatus.hpp"
#include "tools/time.hpp"
#include "quit_to_exit.hpp"
#include "special_items.hpp"
#include "carnival.hpp"

#include "join_request.hpp"

#include <algorithm> // linux

namespace
{
    void world_lock_message(ENetEvent &event, const ::world &world)
    {
        if (world.owner <= 0) return;

        const ::peer *pPeer = static_cast<::peer*>(event.peer->data);
        ::peer owner{.user_id = world.owner};
        
        const bool granted = (pPeer->user_id == world.owner || std::ranges::find(world.access, pPeer->user_id) != world.access.end());
        on::ConsoleMessage(event.peer, std::format("`5[```w{}`` `$World Locked`` by {}{}`5]``", world.name,
            owner.mysql_select<std::string>("growid", "", true), granted ? " (`2ACCESS GRANTED``)" : ""));
    }
}

void action::join_request(ENetEvent& event, const std::string &header, const std::string &world_name = "") 
{
    try 
    {
        ::peer *pPeer = static_cast<::peer*>(event.peer->data);
        ::hPipe hPipe{ header };

        send_varlist(event.peer, { "OnForceTradeEnd" }); // @note sent before OnSendToServer

        std::string name = (world_name.empty()) ? hPipe["name"] : world_name;

        if (name.empty() || name.length() > 24) throw std::runtime_error("Please enter a world name."); // @note client caps at 24
        if (!isalnum(name)) throw std::runtime_error("Sorry, spaces and special characters are not allowed in world or door names.  Try again.");

        for (char &c : name) c = std::toupper(c); // @note start -> START
        if (const std::string forced = special_join_check(event, name); !forced.empty()) name = forced; // @note Curse Wand: HELL. Ban Wand: throws

        action::quit_to_exit(event, "", true); // @note does nothing unless peer is still in a world

        auto it = std::ranges::find(worlds, name, &::world::name);
        if (it == worlds.end()) 
            it = worlds.emplace(worlds.end(), name);
            
        ::world &world = *it;

        const bool has_access = pPeer->user_id == world.owner || pPeer->role >= MODERATOR ||
            std::ranges::find(world.access, pPeer->user_id) != world.access.end();
        if (!has_access && pPeer->level.front() < world.minimum_entry_level)
        {
            const std::string why = std::format("`4You need to be level {} to enter {}.``", static_cast<int>(world.minimum_entry_level), world.name);
            if (world.visitors == 0) // @note nobody else is here, don't keep it loaded
            {
                world.save();
                worlds.erase(it);
            }
            throw std::runtime_error(why);
        }
        carnival_prepare(world); // @note CARNIVAL is built fresh for its first visitor
        {
            ::blob blob = compress_state(::gamePacket{ .type = 0x04, /*PACKET_SEND_MAP_DATA*/ .state = (int)gamePacketState::EXTENDED });
            blob.push_back(world.serialize());

            ENetPacket *packet = enet_packet_create(blob.data().data(), blob.size(), ENET_PACKET_FLAG_RELIABLE);
            if (enet_peer_send(event.peer, 0, packet)) enet_packet_destroy(packet);
        } // @note delete blob
        {
            std::string *this_world = std::ranges::find(pPeer->recent_worlds, world.name);
            std::string *end = pPeer->recent_worlds.end();
            std::string *first = this_world != end ? this_world : pPeer->recent_worlds.begin();

            std::rotate(first, first + 1, end);
            pPeer->recent_worlds.back() = world.name;
        } // @note delete name, first
        on::EmoticonDataChanged(event);

        pPeer->display_growid = std::format("`w{}``", pPeer->growid);
        if (pPeer->user_id == world.owner)
            pPeer->display_growid = std::format("`2{}``", pPeer->growid);
        else if (std::ranges::find(world.access, pPeer->user_id) != world.access.end()) 
            pPeer->display_growid = std::format("`^{}``", pPeer->growid);

        if (pPeer->role >= DEVELOPER)
            pPeer->display_growid = std::format("`6@{}``", pPeer->growid);
        else if (pPeer->role == MODERATOR)
            pPeer->display_growid = std::format("`#@{}``", pPeer->growid);

        pPeer->rest_pos = world.spawn;
        ::pos start = pPeer->rest_pos; // @note where the player appears: the main door, or the door they came through
        if (!pPeer->door_target.empty())
        {
            auto door = std::ranges::find(world.doors, pPeer->door_target, &::door::id);
            if (door != world.doors.end()) start = door->pos.by_32(false);
            pPeer->door_target.clear();
        }
        pPeer->pos = start;

        if (++world.netid_counter == 0) ++world.netid_counter; // @note netid 0 means "not in a world"
        pPeer->netid = world.netid_counter;
        peers(pPeer->recent_worlds.back(), PeerSearch::WORLD, [&event, &pPeer, &world, &start](ENetPeer *peer/*send to everyone in world*/) 
        {
            ::peer *pOthers = static_cast<::peer*>(peer->data); // @note everyone in world's peer.data
            
            if (pOthers->user_id != pPeer->user_id)
            {
                on::Spawn(*event.peer, pOthers->netid, pOthers->user_id, pOthers->pos, pOthers->display_growid, pOthers->country, pOthers->role, pOthers->role >= DEVELOPER, false);
                on::Spawn(*peer, pPeer->netid, pPeer->user_id, start, pPeer->display_growid, pPeer->country, pPeer->role, pPeer->role >= DEVELOPER, false);
                on::SetClothing(peer);
                on::ConsoleMessage(peer, std::format("`5<{} entered, `w{}`` others here>``", pPeer->display_growid, world.visitors));
                send_varlist(peer, {
                    "OnTalkBubble",
                    pPeer->netid,
                    std::format("`5<{} entered, `w{}`` others here>``", pPeer->display_growid, world.visitors),
                    1u
                });
            }
        });
        on::Spawn(*event.peer, pPeer->netid, pPeer->user_id, start, pPeer->display_growid, pPeer->country, pPeer->role, pPeer->role >= DEVELOPER, true);

        if (pPeer->billboard.id != 0) on::BillboardChange(event); // @note don't waste memory if billboard is empty.

        send_varlist(event.peer, {
            "OnSetPos", 
            CL_Vec2f{start.x, start.y}
        }, pPeer->netid);

        on::ConsoleMessage(event.peer, 
            std::format(
                "World `w{}`` entered.  There are `w{}`` other people here, `w{}`` online.", 
                world.name, world.visitors, host->connectedPeers
            )
        );
        world_lock_message(event, world);
        ++world.visitors;
        on::SetClothing(event.peer);
        on::CountryState(event);
        event_buttons_refresh(event.peer); // @note Daily Challenge, Piggy Bank and Mailbox on the left
        special_entered(event, world);

        if (world.base_weather != 0) // @note a blast world's own weather, unless a weather machine is running
        {
            const bool machine_on = valid_tile(world, world.weather) && (world.blocks[cord(world.weather.x_int(), world.weather.y_int())].state[2] & S_TOGGLE);
            if (!machine_on) send_varlist(event.peer, { "OnSetCurrentWeather", static_cast<int>(world.base_weather) });
        }
        send_varlist(event.peer, {
            "OnSetFeatureEnableFlags",
            "EA4PAgcGAQoDEAQRBQ0JCAw="
        });
        if (pPeer->magplant_world == world.name) // @note the game forgets the remote's machine when it loads a world
        {
            auto mag = std::ranges::find(world.magplants, pPeer->magplant, &::magplant::pos);
            if (mag != world.magplants.end() && mag->remote && mag->item != 0)
                send_varlist(event.peer, { "OnPlanterActivated", static_cast<u_int>(mag->item), static_cast<u_int>(mag->pos.x_int()), static_cast<u_int>(mag->pos.y_int()) }, pPeer->netid);
        }
        peers(world.name, PeerSearch::WORLD, [&](ENetPeer *peer)
        {
            const ::peer *pOthers = static_cast<::peer*>(peer->data);
            if (pOthers->user_id != pPeer->user_id)
                send_varlist(event.peer, { "OnTalkBubble", static_cast<u_int>(pOthers->netid), pOthers->growid, 1u });
        });
    }
    catch (const std::exception& exc)
    {
        on::ConsoleMessage(event.peer, exc.what()); // @note tell the player why
        send_varlist(event.peer, { "OnFailedToEnterWorld" });
        return;
    }
}
