#include "pch.hpp"

#include "gateway_edit.hpp"

void gateway_edit(ENetEvent& event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    const short tilex = atoi(hPipe["tilex"].c_str());
    const short tiley = atoi(hPipe["tiley"].c_str());

    auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
    if (world == worlds.end()) return;

    if (tilex < 0 || tilex >= 100 || tiley < 0 || cord(tilex, tiley) >= world->blocks.size()) return;
    if (!world->can_edit(*pPeer, ::pos{tilex, tiley}, false)) return; // @note same rule as building here

    block &block = world->blocks[cord(tilex, tiley)];

    if (hPipe["dialog_name"] == "sign_edit") 
    {
        auto sign = std::ranges::find(world->signs, ::pos{tilex, tiley}, &::sign::pos);
        if (sign != world->signs.end()) 
        {
            sign->label = hPipe["sign_text"];
        }
        else {
            world->signs.emplace_back(::sign(hPipe["sign_text"], ::pos{tilex, tiley}));
        }
    }
    else if (hPipe["dialog_name"] == "door_edit") 
    {
        auto door = std::ranges::find(world->doors, ::pos{tilex, tiley}, &::door::pos);
        if (door != world->doors.end())
        {
            door->label = hPipe["door_name"];
            door->dest = hPipe["door_target"];
            door->id = hPipe["door_id"];
        }
        else door = world->doors.emplace(world->doors.end(), ::door(hPipe["door_name"], hPipe["door_target"], hPipe["door_id"], { tilex, tiley }));

        for (char &c : door->dest) c = std::toupper(c); // @note WORLD:ID, world names are upper case
        for (char &c : door->id) c = std::toupper(c);
        if (door->dest.size() > 24 + 1 + 11) door->dest.resize(24 + 1 + 11);
        if (door->id.size() > 11) door->id.resize(11);
        door->is_public = hPipe["checkbox_locked"] != "0";
    }
    else if (hPipe["dialog_name"] == "gateway_edit") 
    {
        block.state[2] &= ~(S_PUBLIC | S_LOCKED);
        block.state[2] |= atoi(hPipe["checkbox_public"].c_str()) ? S_PUBLIC : S_LOCKED;
    }

    send_tile_update(event, {
        .id = block.fg,
        .punch = { tilex, tiley }
    }, block, *world);
}