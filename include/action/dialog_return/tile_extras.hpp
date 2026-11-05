#pragma once

/* @brief wrench menus for blocks that hold items: Vending Machine, Storage Box Xtreme, Display Block */
extern void vending_wrench(ENetEvent &event, ::world &world, const ::pos &pos);
extern void storage_wrench(ENetEvent &event, ::world &world, const ::pos &pos);
extern void display_wrench(ENetEvent &event, ::world &world, const ::pos &pos);

/* @brief dialog_return handlers */
extern void vending_edit(ENetEvent &event, const ::hPipe &hPipe);
extern void vending_buy(ENetEvent &event, const ::hPipe &hPipe);
extern void storage_edit(ENetEvent &event, const ::hPipe &hPipe);
extern void storage_deposit(ENetEvent &event, const ::hPipe &hPipe);
extern void storage_withdraw(ENetEvent &event, const ::hPipe &hPipe);
extern void display_edit(ENetEvent &event, const ::hPipe &hPipe);
