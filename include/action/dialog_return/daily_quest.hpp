#pragma once

/* @note Crazy Jim's daily quest: two items for one Growtoken, once a day (new quest at midnight UTC) */
extern void show_daily_quest(ENetEvent& event);
extern void daily_quest(ENetEvent& event, const ::hPipe &hPipe);
