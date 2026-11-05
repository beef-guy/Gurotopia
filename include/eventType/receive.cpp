#include "pch.hpp"

#include "action/__action.hpp"
#include "gamePacket/_gamePacket.hpp"

#include "receive.hpp"

void receive(ENetEvent& event)
{
    const ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (event.packet->dataLength == 0ull || pPeer == nullptr)
    {
        enet_packet_destroy(event.packet);
        return;
    }
    try
    {
        switch (event.packet->data[0ull])
        {
            case 2: case 3:
            {
                if (event.packet->dataLength < 5ull) break; // @note 4 byte type + '\0'

                std::string header = {
                    reinterpret_cast<const char*>(event.packet->data + 4),
                    event.packet->dataLength - 4 - sizeof('\0')
                };

                for (char &c : header) if (c == '\n') c = '|';
                const std::vector<std::string> pipes = readch(header, '|');
                if (pipes.size() < 2) break;

                std::string action{};
                if (pipes[0ull] == "protocol" || pipes[0ull] == "tankIDName" || pipes[0ull] == "requestedName")
                {
                    action = (pipes[0ull] == "requestedName") ? "tankIDName" : pipes[0ull];
                }
                else action = std::format("{}|{}", pipes[0ull], pipes[1ull]);

                /* @note login packets carry the password (ltoken is only base64), never write them to the log */
                if (action == "protocol" || action == "tankIDName") print(std::format("{} (login details hidden)", action));
                else print(header.size() > 512ull ? header.substr(0, 512ull) + "..." : header);

                if (!pPeer->authenticated && action != "protocol" && action != "tankIDName" && action != "action|quit") break; // @note must log in first

                if (const auto i = action_pool.find(action); i != action_pool.end())
                    i->second(event, header);
                break;
            }
            case 4:
            {
                if (event.packet->dataLength < sizeof(::gamePacket)) break;

                ::gamePacket gamePacket = make_gamePacket(event.packet->data);
                gamePacket.size = event.packet->dataLength - sizeof(::gamePacket); // @todo did i do this right? or check for flag ::EXTENDED

                if (!pPeer->authenticated && gamePacket.type != 0x1a/*disconnect*/) break; // @note must log in first

                if (const auto i = gamePacket_pool.find(gamePacket.type); i != gamePacket_pool.end())
                    i->second(event, std::move(gamePacket));
                break;
            }
        }
    }
    catch (const std::exception &exc)
    {
        print(std::format("dropped a bad packet: {}", exc.what())); // @note one bad packet should never take the server down
    }
    enet_packet_destroy(event.packet);
}
