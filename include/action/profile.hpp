#pragma once

/*
* @note the buttons of the self wrench menu that open the player's own pages:
*   Wardrobe (the game's own screen), Daily Bonus, Online Status, Notebook, Growmojis, Trades, and the rest.
*/

constexpr int WARDROBE_MAX_SLOTS = 6;

/* @return true if `button` (a buttonClicked from the self wrench menu) was one of these pages */
extern bool profile_button(ENetEvent& event, const std::string &button);

/* @return " (Busy)" / " (Away)" or "" for a player's online status */
extern std::string online_status_text(const ::peer &peer);

namespace action
{
    extern void wardrobe_preset_load(ENetEvent& event, const std::string& header);
    extern void wardrobe_save_ui(ENetEvent& event, const std::string& header);
    extern void wardrobe_load_ui(ENetEvent& event, const std::string& header);
    extern void wardrobe_unequip_all(ENetEvent& event, const std::string& header);
    extern void wardrobe_purchase_slot(ENetEvent& event, const std::string& header);
    extern void dailyrewardmenu(ENetEvent& event, const std::string& header);
    extern void claimdailyreward(ENetEvent& event, const std::string& header);
}

/* @note dialog_returns */
extern void online_status(ENetEvent& event, const ::hPipe &hPipe);
extern void notebook_edit(ENetEvent& event, const ::hPipe &hPipe);
extern void daily_bonus(ENetEvent& event, const ::hPipe &hPipe);
