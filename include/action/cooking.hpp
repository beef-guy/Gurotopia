#pragma once

/*
* @brief Chemical Combiners and Cooking Ovens like real Growtopia (action/cooking.cpp):
*   - Chemical Combiner (Laboratory, E-Z Cook Oven, Microwave, Barbecue Grill, Vehicle Replicator): punch it to open or
*     close it. drop three ingredients on an open one and punch it to make the recipe (resources/combiner.txt).
*   - Cooking Oven (Home Oven, Commercial Oven, Master Chef's Oven, Replicator, Taco Truck Oven): use an ingredient on it.
*     the first one asks for Low, Medium or High heat. add the rest at the right time and punch the oven when everything
*     is Perfect (resources/oven.txt).
*/

/* @return true when this tile change was handled here (an ingredient put in an oven, an oven or combiner punched to make something) */
extern bool cooking_tile_change(ENetEvent &event, ::world &world, ::gamePacket &gamePacket, ::block &block);

/* @brief a block was placed here: a new oven starts empty */
extern void cooking_placed(::world &world, const ::pos &pos, const ::item &item);
/* @brief a block was broken here: forget what the oven had */
extern void cooking_broken(::world &world, const ::pos &pos);

/* @brief dialog_return: the heat for the first ingredient */
extern void oven_heat(ENetEvent &event, const ::hPipe &hPipe);
