#pragma once

/* @note Crazy Jim's daily quest: two items for one Growtoken, once a day (new quest at midnight UTC) */
/* @param phone the Telephone it was called from, nullptr when opened from the menu */
extern void show_daily_quest(ENetEvent& event, const ::pos *phone = nullptr);
/* @brief hand in today's items for a Growtoken. throws the reason when it can't */
extern void deliver_daily_quest(ENetEvent& event);
extern void daily_quest(ENetEvent& event, const ::hPipe &hPipe);
