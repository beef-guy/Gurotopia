#include "pch.hpp"

#include "action/join_request.hpp"
#include "action/quit_to_exit.hpp"
#include "tools/sound.hpp"
#include "action/carnival.hpp"

#include "tile_activate.hpp"

#include <cmath>

void tile_activate(ENetEvent& event, ::gamePacket gamePacket)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (pPeer->netid == 0) return; // @note not in a world

    auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
    if (world == worlds.end()) return;
    if (!valid_tile(*world, gamePacket.punch)) return;

    /* @note doors and checkpoints work by walking into them, so the player has to be right there */
    const ::pos standing = pPeer->pos.by_32(true);
    if (pPeer->role < MODERATOR && (std::abs(gamePacket.punch.x - standing.x) > 2.0f || std::abs(gamePacket.punch.y - standing.y) > 2.0f)) return;

    if (carnival_activate(event, *world, gamePacket.punch)) // @note a Carnival Tent: 1 Golden Ticket to play
    {
        send_gamePacket(*event.peer, gamePacket);
        return;
    }

    ::block &block = world->blocks[cord(gamePacket.punch.x, gamePacket.punch.y)];
    const ::item &item = id_to_item(block.fg);
    switch (item.type)
    {
        case type::MAIN_DOOR:
        {
            play_sfx(event.peer, "audio/door_shut.wav");
            action::quit_to_exit(event, "", false);
            break;
        }
        case type::DOOR: // @todo add door-to-door with door::id
        case type::PORTAL:
        {
            auto door = std::ranges::find(world->doors, gamePacket.punch, &::door::pos);
            if (door != world->doors.end() && !door->is_public && !world->can_edit(*pPeer, gamePacket.punch, false))
            {
                send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, "The door is locked.", 0u, 1u }); // @note not open to public
                play_sfx(event.peer, "audio/punch_locked.wav");
                send_varlist(event.peer, ::VariantList{ "OnSetFreezeState", 0u }, pPeer->netid);
                break;
            }
            std::string dest_world{}, dest_id{};
            if (door != world->doors.end() && !door->dest.empty())
            {
                const std::size_t colon = door->dest.find(':');
                dest_world = door->dest.substr(0, colon);
                if (colon != std::string::npos) dest_id = door->dest.substr(colon + 1);
            }
            if (!dest_world.empty() && dest_world != world->name)
            {
                pPeer->door_target = dest_id; // @note WORLD:ID appears at that door
                play_sfx(event.peer, "audio/door_open.wav");
                action::quit_to_exit(event, "", true); // @note leaving can delete this world, door goes with it
                action::join_request(event, "", dest_world);
            }
            else if (!dest_id.empty()) // @note :ID (or this world's name) moves to another door here
            {
                auto target = std::ranges::find(world->doors, dest_id, &::door::id);
                const ::pos to = (target != world->doors.end()) ? target->pos.by_32(false) : world->spawn;
                pPeer->pos = to;
                send_varlist(event.peer, ::VariantList{ "OnSetPos", CL_Vec2f{to.x, to.y} }, pPeer->netid);
                play_in_world(world->name, pPeer->netid, "audio/door_open.wav");
                send_varlist(event.peer, ::VariantList{ "OnZoomCamera", 10000.0f, 1000u });
                send_varlist(event.peer, ::VariantList{ "OnSetFreezeState", 0u }, pPeer->netid);
                peers(world->name, PeerSearch::WORLD, [&](ENetPeer *peer) // @note others see the player move too
                {
                    if (peer != event.peer) send_varlist(peer, ::VariantList{ "OnSetPos", CL_Vec2f{to.x, to.y} }, pPeer->netid);
                });
            }
            else {
                send_varlist(event.peer, ::VariantList{
                    "OnSetPos", 
                    CL_Vec2f{pPeer->rest_pos.x, pPeer->rest_pos.y}
                }, pPeer->netid);
                send_varlist(event.peer, ::VariantList{
                    "OnZoomCamera",
                    10000.0f,
                    1000u
                });
                send_varlist(event.peer, ::VariantList{
                    "OnSetFreezeState", 
                    0u
                }, pPeer->netid);
                play_in_world(world->name, pPeer->netid, "audio/teleport.wav");
            }
            break;
        }
        case type::CHECKPOINT:
        {
            if (valid_tile(*world, pPeer->rest_pos.by_32(true)))
            {
                ::block &checkpoint = world->blocks[cord(pPeer->rest_pos.by_32(true).x, pPeer->rest_pos.by_32(true).y)]; // @note get previous checkpoint from respawn position

                checkpoint.state[2] &= ~S_TOGGLE;
                send_tile_update(event, ::gamePacket{.id = block.fg/*has to be 'block' or else iterfere with main door*/, .punch = pPeer->rest_pos.by_32(true)}, checkpoint, *world);
            }

            pPeer->rest_pos = gamePacket.punch.by_32();
            block.state[2] |= S_TOGGLE; // @note toggle current checkpoint
            send_tile_update(event, ::gamePacket{.id = block.fg, .punch = gamePacket.punch}, block, *world);
            break;
        }
    }

    send_gamePacket(*event.peer, gamePacket); // finished.
}