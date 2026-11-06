#include "pch.hpp"

#include "sound.hpp"

void play_sfx(ENetPeer *peer, std::string_view file, int delay)
{
    send_gameMessage(peer, {.message = std::format("action|play_sfx\nfile|{}\ndelayMS|{}", file, delay)});
}

void play_in_world(const std::string &world, int netid, std::string_view file, int delay)
{
    peers(world, PeerSearch::WORLD, [&](ENetPeer *peer)
    {
        send_varlist(peer, { "OnPlayPositioned", std::string(file) }, netid, delay);
    });
}
