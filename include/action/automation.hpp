#pragma once

/*
* @brief gem-fuelled machines like real Growtopia:
* Tesseract Manipulator (breaks a target block), Heart of Gaia (harvests a target tree),
* Techno-Organic Engine (either, and it keeps the target's drops inside).
* wrench to pick the target and add gems, punch to turn it on or off.
*/

extern void auto_machine_wrench(ENetEvent &event, ::world &world, const ::pos &pos);
extern void auto_machine_edit(ENetEvent &event, const ::hPipe &hPipe);

/* @brief punching turns it on or off. @return true when the punch must not damage it (it still holds gems or items) */
extern bool auto_machine_punch(ENetEvent &event, ::world &world, const ::pos &pos, bool last_hit);

/* @brief runs the machines in every world someone is in. call often from the main loop */
extern void automation_tick();
