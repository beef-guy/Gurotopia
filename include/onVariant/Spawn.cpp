#include "pch.hpp"

#include "Spawn.hpp"

void on::Spawn(ENetPeer &peer, signed netID, signed userID, ::pos posXY, std::string name, std::string country, bool mstate, bool smstate, bool local, std::string title_icon) 
{
    if (name.ends_with("``")) name.resize(name.size() - 2); // @note added back below. a name must end in exactly " of Legend``" for the Legend flag
    send_varlist(&peer, { 
        "OnSpawn", 
        std::format(
            "spawn|avatar\nnetID|{}\nuserID|{}\ncolrect|0|0|20|30\nposXY|{}|{}\nname|{}``\n{}country|{}\ninvis|0\nmstate|{}\nsmstate|{}\nonlineID|\n{}",
            netID, userID, posXY.by_32(true).x_int(), posXY.by_32(true).y_int(), name, title_icon.empty() ? "" : std::format("titleIcon|{}\n", title_icon), country, to_char(mstate), to_char(smstate), (local) ? "type|local\n" : ""
        )
    }, -1, 0xffffffff);
}