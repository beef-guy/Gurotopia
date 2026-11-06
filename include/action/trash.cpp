#include "pch.hpp"

#include "tools/create_dialog.hpp"

#include "trash.hpp"

void action::trash(ENetEvent& event, const std::string& header)
{
    const std::vector<std::string> pipes = readch(header, '|');
    if (pipes.size() < 5ull) return;

    const ::item &item = id_to_item(atoi(pipes[4ull].c_str()));

    if (item.type == type::FIST || item.type == type::WRENCH)
    {
        send_varlist(event.peer, { "OnTextOverlay", "You'd be sorry if you lost that!" });
        return;
    }
    // @todo add confirm message on untradeables
    
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    for (const ::slot &slot : pPeer->slots)
        if (slot.id == item.id)
        {
            send_varlist(event.peer, {
                "OnDialogRequest",
                create_dialog()
                    .set_default_color("`o")
                    .add_label_with_icon("big", std::format("`4Recycle`` `w{}``", item.raw_name), slot.id)
                    .add_textbox(std::format("How many to `4destroy``? (you have {})", slot.count))
                    .add_textbox((item.cat & CAT_UNTRADEABLE) || item.rarity <= 0 || item.rarity >= 999 ? 
                        std::string("`oThis item can't be recycled for gems.``") :
                        std::format("`oYou will get `w{}`` gems for every 50 recycled.``", item.rarity))
                    .add_text_input("count", "", 0, 5)
                    .embed_data("itemID", slot.id)
                    .end_dialog("trash_item")
            });
            return;
        }
}