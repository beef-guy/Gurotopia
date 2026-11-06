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

    if (!blast::is_world_blast(id) || pPeer->have_slot(::slot(static_cast<short>(id), 0)) == 0) return;
    if (id == 830/*Beach Blast*/ && pPeer->have_slot(::slot(834, 0)) < 100)
    {
        send_varlist(event.peer, { "OnTextOverlay", "You need 100 Fireworks to detonate this!" });
        return;
    }
    if (::world::exists(world_name)) // @note checks saved worlds too, a blast must never wipe someone's world
    {
        send_varlist(event.peer, { "OnTextOverlay", "That world already exists. Try another name." });
        return;
    }
    action::quit_to_exit(event, "", true);
    {
        ::world world{world_name}; // @note we don't need emplace into worlds since no one is in the world yet!
        if (id == 1402) blast::thermonuclear(world); // @note Thermonuclear Blast
        else blast::themed(world, id);
    } // @note save the world and continue in action::join_request. seems tedious so i might change later.
    modify_item_inventory(event, ::slot(static_cast<short>(id), -1));
    if (id == 830) modify_item_inventory(event, ::slot(834, -100)); // @note Beach Blast burns 100 Fireworks
    action::join_request(event, "", world_name);
}
