#pragma once

/*
* @brief transform items: wear one, wrench yourself and press its Transform button (or use its key/crystal/compass on yourself),
*        and the game client switches the item to its "transformed" look (Space Badger, Mask of The Dragon, Go-Go-Growformer!...).
* @note the client does the drawing itself: the server only sends OnSetItemTransform(item id) / OnClearItemTransforms
*       for that player (from the 5.60 client: NetAvatar::OnSetItemTransform asks the item's behaviour to transform).
* @note the buttons are the ones in the game folder: interface/large/gui_wrench_*transform*.rttex and friends.
*/

/* @return the Transform custom buttons for what this player wears ("" if none), for the self wrench menu */
extern std::string transform_buttons(const ::peer &peer);

/* @return true if the wrench menu button was a Transform button (and it was handled) */
extern bool transform_button_clicked(ENetEvent &event, const std::string &button);

/* @return true if this consumable is a transform key (Growformer PowerKey, Mystic Transform Crystal, Magical Captain's Compass) and it was used */
extern bool transform_consumable(ENetEvent &event, short id);

/* @brief clothes changed: a transform whose item is no longer worn ends. Diamond Horns turn into the next flavour when taken off */
extern void transform_clothes_changed(ENetEvent &event, u_short was_wearing, u_short now_wearing);

/* @brief this player just entered a world: show them everyone's transform, and show theirs to everyone */
extern void transform_entered(ENetEvent &event);
