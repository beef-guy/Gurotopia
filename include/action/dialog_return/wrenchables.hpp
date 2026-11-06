#pragma once

/* @brief wrench menus for the Telephone, Mailbox, Bulletin Board and Donation Box */
extern void telephone_wrench(ENetEvent &event, ::world &world, const ::pos &pos);
extern void mailbox_wrench(ENetEvent &event, ::world &world, const ::pos &pos);
extern void bulletin_wrench(ENetEvent &event, ::world &world, const ::pos &pos);
extern void donation_wrench(ENetEvent &event, ::world &world, const ::pos &pos);

/* @brief dialog_return handlers */
extern void telephone(ENetEvent &event, const ::hPipe &hPipe);
extern void mailbox_edit(ENetEvent &event, const ::hPipe &hPipe);
extern void bulletin_edit(ENetEvent &event, const ::hPipe &hPipe);
extern void donation_edit(ENetEvent &event, const ::hPipe &hPipe);
extern void donation_give(ENetEvent &event, const ::hPipe &hPipe);

/* @brief Display Shelf: four items on show */
extern void shelf_wrench(ENetEvent &event, ::world &world, const ::pos &pos);
extern void shelf_edit(ENetEvent &event, const ::hPipe &hPipe);
