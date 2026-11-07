#include "pch.hpp"

#include "dialog_return/__dialog_return.hpp"

#include "action/dialog_return.hpp"
#include "scripting/lua.hpp"

void action::dialog_return(ENetEvent& event, const std::string& header) 
{
    if (lua::dialog(event.peer, header)) return; // @note a script's dialog (named lua_...)

    ::hPipe hPipe{ header };

    // @note found at ./action/dialog_return/
    if (const auto it = dialog_return_pool.find(hPipe["dialog_name"]); it != dialog_return_pool.end()) 
    {
        try
        {
            it->second(std::ref(event), std::move(hPipe));
        }
        catch (const std::exception &exc) // @note tell the player why it didn't work
        {
            const ::peer *pPeer = static_cast<::peer*>(event.peer->data);
            send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, exc.what(), 0u, 1u });
        }
    }
}