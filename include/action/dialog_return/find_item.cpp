#include "pch.hpp"

#include "onVariant/SetClothing.hpp"
#include "onVariant/ConsoleMessage.hpp"

#include "find_item.hpp"

void find_item(ENetEvent& event, const ::hPipe &hPipe)
{
    const std::vector<std::string> button = readch(hPipe["buttonClicked"], '_'); // e.g. searchableItemListButton_2_0_-1
    if (button.size() < 2ull) return; // @note dialog closed without picking an item

    const int id = atoi(button[1ull].c_str());
    if (id <= 0 || static_cast<std::size_t>(id) >= items.size()) return;

    modify_item_inventory(event, ::slot(id, 200));
    on::ConsoleMessage(event.peer, std::format("Added `w200 {}`` to your backpack.", id_to_item(id).raw_name));
}
