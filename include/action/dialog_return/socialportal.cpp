#include "pch.hpp"

#include "database/friends.hpp"

#include "socialportal.hpp"

void socialportal(ENetEvent& event, const ::hPipe &hPipe)
{
    if (hPipe["buttonClicked"] == "showfriend") show_friends(event, false);
}
