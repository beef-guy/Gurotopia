#pragma once

extern void tile_change(ENetEvent& event, ::gamePacket gamePacket);

/* @brief tell everyone in the world which tiles an area lock now holds */
extern void refresh_lock_tiles(ENetEvent &event, ::world &world, const ::pos &lock_pos);
/* @brief the lock was broken: free its tiles */
extern void remove_area_lock(ENetEvent &event, ::world &world, const ::pos &lock_pos);
