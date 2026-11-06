#pragma once

/* @brief wrench menus for the Telephone, Mailbox, Bulletin Board and Donation Box */
extern void telephone_wrench(ENetEvent &event, ::world &world, const ::pos &pos);
extern void mailbox_wrench(ENetEvent &event, ::world &world, const ::pos &pos);
extern void bulletin_wrench(ENetEvent &event, ::world &world, const ::pos &pos);
extern void donation_wrench(ENetEvent &event, ::world &world, const ::pos &pos);
/* @brief an item tapped onto a Donation Box: ask how many to give */
extern void donation_offer(ENetEvent &event, ::world &world, const ::pos &pos, short id);

/* @brief dialog_return handlers */
extern void telephone(ENetEvent &event, const ::hPipe &hPipe);
extern void mailbox_edit(ENetEvent &event, const ::hPipe &hPipe);
extern void bulletin_edit(ENetEvent &event, const ::hPipe &hPipe);
extern void donation_edit(ENetEvent &event, const ::hPipe &hPipe);
extern void donation_give(ENetEvent &event, const ::hPipe &hPipe);

/* @brief Display Shelf: four items on show */
extern void shelf_wrench(ENetEvent &event, ::world &world, const ::pos &pos);
extern void shelf_edit(ENetEvent &event, const ::hPipe &hPipe);

/* @brief resend every Heart Monitor that follows this player (call when they log on or off) */
extern void heart_monitors_refresh(int uid);

/* @brief Mannequin: clothing used on it (asks first), wrench for the sign text and taking clothes off */
extern void mannequin_offer(ENetEvent &event, ::world &world, const ::pos &pos, short id);
extern void mannequin_wrench(ENetEvent &event, ::world &world, const ::pos &pos);
extern void mannequin_dress(ENetEvent &event, const ::hPipe &hPipe);
extern void mannequin_edit(ENetEvent &event, const ::hPipe &hPipe);

/* @brief Dice and Roshambo blocks: "Usable by public" and "Silenced" */
extern void dice_wrench(ENetEvent &event, ::world &world, const ::pos &pos);
extern void dice_edit(ENetEvent &event, const ::hPipe &hPipe);

/* @brief Weather Machine - Background (pick a background) and - Stuff (item that falls, gravity) */
extern void weather_wrench(ENetEvent &event, ::world &world, const ::pos &pos);
extern void weather_edit(ENetEvent &event, const ::hPipe &hPipe);
