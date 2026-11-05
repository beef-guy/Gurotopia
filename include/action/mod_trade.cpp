#include "pch.hpp"

#include "tools/create_dialog.hpp"

#include "mod_trade.hpp"

void action::mod_trade(ENetEvent& event, const std::string& header) 
{
    const std::vector<std::string> pipes = readch(header, '|');
    if (pipes.size() < 4ull) return;

    const ::item &item = id_to_item(atoi(pipes[3ull].c_str()));

    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (pPeer->trade_with == 0) return; // @note not trading
    if (item.cat & CAT_UNTRADEABLE)
    {
        send_varlist(event.peer, { "OnTextOverlay", "You can't trade that." });
        return;
    }

    for (const ::slot &slot : pPeer->slots)
        if (slot.id == item.id) 
        {
            send_varlist(event.peer, { 
                "OnDialogRequest", 
                create_dialog()
                    .set_default_color("`o")
                    .add_label_with_icon("big", std::format("`2Trade`` `w{}``", item.raw_name), item.id)
                    .add_textbox("`2Trade how many?``")
                    .add_text_input("count", "", slot.count, 5)
                    .embed_data("itemID", item.id)
                    .end_dialog("trade_item")
            });
            return;
        }
}
