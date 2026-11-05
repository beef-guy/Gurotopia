#include "pch.hpp"

#include "action/quit_to_exit.hpp" // @note peer leave world
#include "action/join_request.hpp" // @note peer enter (blast) world

#include "create_blast.hpp"

void create_blast(ENetEvent& event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    const int id = atoi(hPipe["id"].c_str());
    std::string world_name = hPipe["name"];

    if (world_name.empty() || world_name.length() > 24 || !isalnum(world_name))
    {
        send_varlist(event.peer, { "OnTextOverlay", "World names can only use letters and numbers." });
        return;
    }
    for (char &c : world_name) c = std::toupper(c); // @note start -> START

    switch (id)
    {
        case 1402: // @note Thermonuclear Blast
        {
            if (pPeer->have_slot(::slot(id, 0)) == 0) return;
            if (::world::exists(world_name)) // @note checks saved worlds too, a blast must never wipe someone's world
            {
                send_varlist(event.peer, { "OnTextOverlay", "That world already exists. Try another name." });
                return;
            }
            action::quit_to_exit(event, "", true);
            {
                ::world world{world_name}; // @note we don't need emplace into worlds since no one is in the world yet!
                blast::thermonuclear(world);
            } // @note save the world and continue in action::join_request. seems tedious so i might change later.
            modify_item_inventory(event, ::slot(id, -1));
            break;
        }
        default: return;
    }
    action::join_request(event, "", world_name);
}
