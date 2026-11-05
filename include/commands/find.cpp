#include "pch.hpp"

#include "find.hpp"

void find(ENetEvent& event, const std::string_view text)
{
    constexpr std::size_t prefix = sizeof("/find ") - 1;

    /* @note /find wings opens the dialog with "wings" already typed */
    std::string search = (text.size() > prefix) ? std::string{ text.substr(prefix) } : std::string{};
    std::erase_if(search, [](char c) { return c == '|' || c == '\n' || c == '\r' || c == '`'; }); // @note these would break the dialog
    if (search.size() > 26ull) search.resize(26ull);

    send_varlist(event.peer, {
        "OnDialogRequest",
        std::format(
            "set_default_color|`o\n"
            "add_label_with_icon|big|`wItem Finder``|left|6016|\n"
            "add_smalltext|Search by name, then tap an item to get `w200`` of it.|left|\n"
            "add_spacer|small|\n"
            "add_text_input|n|`wSearch:`` |{}|26|\n"
            "add_searchable_item_list||sourceType:allItems;listType:iconWithCustomLabel;resultLimit:60|n|\n"
            "add_quick_exit|\n"
            "end_dialog|find_item|||",
            search
        )
    });
}
