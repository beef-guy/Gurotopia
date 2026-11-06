#include "pch.hpp"
#include "gamePacket/tile_change.hpp"

#include "onVariant/ConsoleMessage.hpp"
#include "tools/sound.hpp"

#include "lock_edit.hpp"

#include <algorithm>

void lock_edit(ENetEvent& event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
    if (world == worlds.end()) return;

    ::pos pos{};
    pos.x = atoi(hPipe["tilex"].c_str());
    pos.y = atoi(hPipe["tiley"].c_str());
    if (pos.x < 0 || pos.x >= 100 || pos.y < 0 || cord(pos.x, pos.y) >= world->blocks.size()) return;

    if (const short fg = world->blocks[cord(pos.x, pos.y)].fg; is_tile_lock(fg))
    {
        ::area_lock *lock = world->lock_at(pos);
        if (lock == nullptr || (!pPeer->owns(lock->owner) && pPeer->role < MODERATOR)) return;

        lock->is_public = atoi(hPipe["checkbox_public"].c_str());
        if (lock->is_public) // @note the client draws a public lock's border yellow instead of green
             world->blocks[cord(pos.x, pos.y)].state[2] |= S_PUBLIC;
        else world->blocks[cord(pos.x, pos.y)].state[2] &= ~S_PUBLIC;
        const bool ignore_air = atoi(hPipe["checkbox_ignore"].c_str());
        if (fg == 4994/*Builder's Lock*/)
            lock->builder_mode = atoi(hPipe["checkbox_buildonly"].c_str()) ? 1 : atoi(hPipe["checkbox_breakonly"].c_str()) ? 2 : 0;

        std::erase_if(lock->access, [&](int uid) { return hPipe[std::format("checkbox_access_{}", uid)] == "0"; });
        if (const std::string picked = hPipe["playerNetID"]; !picked.empty())
        {
            const int netid = atoi(picked.c_str());
            peers(world->name, PeerSearch::WORLD, [&](ENetPeer *peer)
            {
                ::peer *pOthers = static_cast<::peer*>(peer->data);
                if (pOthers->netid != netid || pOthers->user_id == lock->owner) return;
                if (std::ranges::find(lock->access, pOthers->user_id) != lock->access.end()) return;
                if (lock->access.size() >= 20) { on::ConsoleMessage(event.peer, "`4The access list is full.``"); return; }

                lock->access.push_back(pOthers->user_id);
                on::ConsoleMessage(event.peer, std::format("`w{}`` was added to the access list.", pOthers->growid));
                on::ConsoleMessage(peer, std::format("`2You got access to a `w{}`` from {}.``", id_to_item(fg).raw_name, pPeer->growid));
                play_sfx(peer, "audio/secret.wav");
            });
        }
        if (hPipe["buttonClicked"] == "recalcLock" || ignore_air != lock->ignore_air)
        {
            lock->ignore_air = ignore_air;
            const std::vector<int> before = [&]{
                std::vector<int> tiles{};
                const int lock_index = cord(pos.x, pos.y);
                for (int i = 0; i < static_cast<int>(world->blocks.size()); ++i)
                    if ((world->blocks[i].state[2] & S_PARENT) && (world->blocks[i].state[0] | (world->blocks[i].state[1] << 8)) == lock_index) tiles.push_back(i);
                return tiles;
            }();
            world->apply_area_lock(*lock, fg);
            for (const int i : before) // @note tiles the lock let go of
                if (!(world->blocks[i].state[2] & S_PARENT))
                    send_tile_update(event, {.id = world->blocks[i].fg, .punch = ::pos{i % 100, i / 100}}, world->blocks[i], *world);
            refresh_lock_tiles(event, *world, pos);
            on::ConsoleMessage(event.peer, std::format("`wThe `${}`` was re-applied.``", id_to_item(fg).raw_name));
        }
        else send_tile_update(event, {.id = fg, .punch = pos}, world->blocks[cord(pos.x, pos.y)], *world);
        return;
    }

    if (!pPeer->owns(world->owner)) return; // @note only the owner can change the lock
    if (id_to_item(world->blocks[cord(pos.x, pos.y)].fg).type != type::LOCK) return;
    
    world->is_public = atoi(hPipe["checkbox_public"].c_str());
    if (atoi(hPipe["checkbox_disable_music"].c_str()) != 0)
        world->lock_state |= DISABLE_MUSIC;
    else world->lock_state &= ~DISABLE_MUSIC;
    world->minimum_entry_level = static_cast<u_char>(std::clamp(atoi(hPipe["minimum_entry_level"].c_str()), 1, 125));

    /* @note unticked names lose access */
    for (int &uid : world->access)
    {
        if (uid > 0 && hPipe[std::format("checkbox_access_{}", uid)] == "0")
        {
            on::ConsoleMessage(event.peer, "`wRemoved a player from the access list.``");
            uid = 0;
        }
    }
    /* @note the player picker sends the picked player's netID */
    if (const std::string picked = hPipe["playerNetID"]; !picked.empty())
    {
        const int netid = atoi(picked.c_str());
        peers(world->name, PeerSearch::WORLD, [&](ENetPeer *peer)
        {
            ::peer *pOthers = static_cast<::peer*>(peer->data);
            if (pOthers->netid != netid || pOthers->user_id == pPeer->user_id || pOthers->user_id == world->owner) return;
            if (std::ranges::find(world->access, pOthers->user_id) != world->access.end()) return;

            auto empty = std::ranges::find(world->access, 0);
            if (empty == world->access.end())
            {
                on::ConsoleMessage(event.peer, "`4The access list is full.``");
                return;
            }
            *empty = pOthers->user_id;
            on::ConsoleMessage(event.peer, std::format("`w{}`` was added to the access list.", pOthers->growid));
            on::ConsoleMessage(peer, std::format("`2You got access to `w{}`` from {}.``", world->name, pPeer->growid));
            pOthers->display_growid = std::format("`^{}``", pOthers->growid);
        });
    }

    ::block &block = world->blocks[cord(pos.x, pos.y)];

    if (world->is_public) 
         block.state[2] |= S_PUBLIC;
    else block.state[2] &= ~S_PUBLIC;
    on::ConsoleMessage(event.peer, std::format("`2{}`` has set the `$World Lock`` to {}``", pPeer->growid, (block.state[2] & S_PUBLIC) ? "`$PUBLIC" : "`4PRIVATE"));

    send_tile_update(event, {
        .id = block.fg,
        .punch = pos
    }, block, *world);
}