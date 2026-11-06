#pragma once

/*
* @brief fishing like real Growtopia: equip a rod, use bait on the water in front of your feet, wait for a bite, then punch.
* also the Fish Tank Port, Fish Wall Mounts and the Lobster Trap.
*/

/* @return true when this tile change was fishing (casting bait, or punching to reel in) */
extern bool fishing_tile_change(ENetEvent &event, ::world &world, const ::gamePacket &gamePacket);

/* @brief stop fishing when the player walks away */
extern void fishing_moved(ENetEvent &event);

/* @brief bites and splashes. call often from the main loop */
extern void fishing_tick();

/* @brief the heaviest a fish can be in normal fishing (lbs) */
extern int fish_max_size(short fish);

/* @brief Fish Tank Port, Fish Wall Mount, Lobster Trap */
extern void fish_tank_wrench(ENetEvent &event, ::world &world, const ::pos &pos);
extern void fish_tank_edit(ENetEvent &event, const ::hPipe &hPipe);
extern void fish_mount_wrench(ENetEvent &event, ::world &world, const ::pos &pos);
extern void fish_mount_edit(ENetEvent &event, const ::hPipe &hPipe);
extern void lobster_trap_wrench(ENetEvent &event, ::world &world, const ::pos &pos);
extern void lobster_trap_edit(ENetEvent &event, const ::hPipe &hPipe);
/* @brief punching a Lobster Trap checks it. @return true if something happened (the punch shouldn't break it) */
extern bool lobster_trap_punch(ENetEvent &event, ::world &world, const ::pos &pos);
