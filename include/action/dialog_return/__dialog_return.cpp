#include "pch.hpp"

#include "popup.hpp"

#include "drop_item.hpp"
#include "trash_item.hpp"
#include "find_item.hpp"
#include "trade_item.hpp"
#include "action/trade.hpp"

#include "gateway_edit.hpp"

#include "billboard_edit.hpp"
#include "lock_edit.hpp"

#include "create_blast.hpp"
#include "socialportal.hpp"
#include "friends.hpp"
#include "daily_quest.hpp"
#include "megaphone.hpp"
#include "tile_extras.hpp"
#include "wrenchables.hpp"
#include "action/fishing.hpp"
#include "action/automation.hpp"
#include "action/event_buttons.hpp"
#include "action/profile.hpp"
#include "action/growpass.hpp"
#include "action/special_items.hpp"

#include "__dialog_return.hpp"

std::unordered_map<std::string, std::function<void(ENetEvent &, const ::hPipe &)>> dialog_return_pool
{
    {"popup", std::bind(&popup, std::placeholders::_1, std::placeholders::_2)},

    {"drop_item", std::bind(&drop_item, std::placeholders::_1, std::placeholders::_2)},
    {"trash_item", std::bind(&trash_item, std::placeholders::_1, std::placeholders::_2)},
    {"find_item", std::bind(&find_item, std::placeholders::_1, std::placeholders::_2)},
    {"trade_item", std::bind(&trade_item, std::placeholders::_1, std::placeholders::_2)},
    {"trade_confirm", std::bind(&trade_confirm, std::placeholders::_1, std::placeholders::_2)},

    {"gateway_edit", std::bind(&gateway_edit, std::placeholders::_1, std::placeholders::_2)},
    {"door_edit", std::bind(&gateway_edit, std::placeholders::_1, std::placeholders::_2)},
    {"sign_edit", std::bind(&gateway_edit, std::placeholders::_1, std::placeholders::_2)},

    {"billboard_edit", std::bind(&billboard_edit, std::placeholders::_1, std::placeholders::_2)},
    {"lock_edit", std::bind(&lock_edit, std::placeholders::_1, std::placeholders::_2)},

    {"create_blast", std::bind(&create_blast, std::placeholders::_1, std::placeholders::_2)},
    {"socialportal", std::bind(&socialportal, std::placeholders::_1, std::placeholders::_2)},
    {"friends", std::bind(&friends, std::placeholders::_1, std::placeholders::_2)},
    {"daily_quest", std::bind(&daily_quest, std::placeholders::_1, std::placeholders::_2)},
    {"piggy_bank", std::bind(&piggy_bank, std::placeholders::_1, std::placeholders::_2)},
    {"friend_edit", std::bind(&friend_edit, std::placeholders::_1, std::placeholders::_2)},
    {"megaphone", std::bind(&megaphone, std::placeholders::_1, std::placeholders::_2)},

    {"vending", std::bind(&vending_edit, std::placeholders::_1, std::placeholders::_2)},
    {"vending_buy", std::bind(&vending_buy, std::placeholders::_1, std::placeholders::_2)},
    {"kranken", std::bind(&kranken_edit, std::placeholders::_1, std::placeholders::_2)},
    {"vault", std::bind(&vault_edit, std::placeholders::_1, std::placeholders::_2)},
    {"game_rules", std::bind(&game_rules_edit, std::placeholders::_1, std::placeholders::_2)},
    {"game_tile", std::bind(&game_tile_edit, std::placeholders::_1, std::placeholders::_2)},
    {"challenge", std::bind(&challenge_edit, std::placeholders::_1, std::placeholders::_2)},
    {"storage", std::bind(&storage_edit, std::placeholders::_1, std::placeholders::_2)},
    {"storage_deposit", std::bind(&storage_deposit, std::placeholders::_1, std::placeholders::_2)},
    {"storage_withdraw", std::bind(&storage_withdraw, std::placeholders::_1, std::placeholders::_2)},
    {"display", std::bind(&display_edit, std::placeholders::_1, std::placeholders::_2)},
    {"xenonite_edit", std::bind(&xenonite_edit, std::placeholders::_1, std::placeholders::_2)},
    {"telephone", std::bind(&telephone, std::placeholders::_1, std::placeholders::_2)},
    {"mailbox", std::bind(&mailbox_edit, std::placeholders::_1, std::placeholders::_2)},
    {"bulletin", std::bind(&bulletin_edit, std::placeholders::_1, std::placeholders::_2)},
    {"donation", std::bind(&donation_edit, std::placeholders::_1, std::placeholders::_2)},
    {"donation_give", std::bind(&donation_give, std::placeholders::_1, std::placeholders::_2)},
    {"shelf", std::bind(&shelf_edit, std::placeholders::_1, std::placeholders::_2)},
    {"mannequin", std::bind(&mannequin_edit, std::placeholders::_1, std::placeholders::_2)},
    {"mannequin_dress", std::bind(&mannequin_dress, std::placeholders::_1, std::placeholders::_2)},
    {"dice", std::bind(&dice_edit, std::placeholders::_1, std::placeholders::_2)},
    {"weather", std::bind(&weather_edit, std::placeholders::_1, std::placeholders::_2)},
    {"magplant", std::bind(&magplant_edit, std::placeholders::_1, std::placeholders::_2)},
    {"auto_machine", std::bind(&auto_machine_edit, std::placeholders::_1, std::placeholders::_2)},
    {"report_player", std::bind(&report_player, std::placeholders::_1, std::placeholders::_2)},
    {"growscan", std::bind(&growscan_edit, std::placeholders::_1, std::placeholders::_2)},
    {"fish_tank", std::bind(&fish_tank_edit, std::placeholders::_1, std::placeholders::_2)},
    {"fish_mount", std::bind(&fish_mount_edit, std::placeholders::_1, std::placeholders::_2)},
    {"lobster_trap", std::bind(&lobster_trap_edit, std::placeholders::_1, std::placeholders::_2)},
    {"online_status", std::bind(&online_status, std::placeholders::_1, std::placeholders::_2)},
    {"notebook_edit", std::bind(&notebook_edit, std::placeholders::_1, std::placeholders::_2)},
    {"growmojis", std::bind(&growmojis_edit, std::placeholders::_1, std::placeholders::_2)},
    {"grow_pass", std::bind(&grow_pass_edit, std::placeholders::_1, std::placeholders::_2)}
};