/*
    @copyright gurotopia (c) 2024-05-25
    @version parent SHA: 313310dc606c852ed72b64e7e325a15eee3be42b 2026-11-3
*/
#include "include/pch.hpp"
#include "include/eventType/_eventType.hpp"

#include "include/database/shouhin.hpp" // @include parse_store()
#include "include/https/https.hpp" // @include https::listener()
#include "include/https/server_data.hpp" // @include gServer_data
#include "include/database/database.hpp"
#include "include/automate/holiday.hpp"
#include "include/tools/terminal.hpp"

#include <chrono>
#include <csignal>
#include <thread>

namespace
{
    volatile std::sig_atomic_t gSignal = 0;
}
static void signal_handler(int signal) { gSignal = signal; }

/* @note save everyone online and every open world */
static void save_everything()
{
    peers("", PeerSearch::EVERYONE, [](ENetPeer *peer)
    {
        ::peer *pPeer = static_cast<::peer*>(peer->data);
        if (pPeer != nullptr && pPeer->authenticated) pPeer->mysql_update_all();
    });
    for (::world &world : worlds) world.save();
}

int main()
{
    std::signal(SIGINT, signal_handler);
#ifdef SIGHUP // @note unix
    std::signal(SIGHUP, signal_handler); // @note PuTTY, SSH problems
#endif
#ifdef SIGBREAK // @note windows, closing the console
    std::signal(SIGBREAK, signal_handler);
#endif

    try
    {
        mysql_library_init(0, NULL, NULL);
        enet_initialize();
        {
            gServer_data.init(); // @note ./server_data.php
            std::thread(&https::listener).detach();

            ENetAddress address{
                .type = ENET_ADDRESS_TYPE_IPV4,
                .port = gServer_data.port
            };
            host = enet_host_create (ENET_ADDRESS_TYPE_IPV4, &address, 50ull/* max peer count */, 2ull, 0u, 0u);
        } // @note delete address
        if (host == nullptr) throw std::runtime_error(std::format("could not open port {}. is the server already running?", gServer_data.port));

        host->usingNewPacketForServer = true;
        host->checksum = enet_crc32;
        enet_host_compress_with_range_coder(host);

        db.load_login(); // @note ./mysql_login.txt
        db.mysql_connect();

        decode_items_dat();
        parse_store();
        check_for_holiday();
    }
    catch (const std::exception &exc)
    {
        print(std::format("the server could not start: {}", exc.what()));
        fflush(stdout);
        return EXIT_FAILURE;
    }

    using clock = std::chrono::steady_clock;
    clock::time_point last_save = clock::now();

    ENetEvent event{};
    while (!gSignal)
    {
        if (enet_host_service(host, &event, 1000/*ms*/) > 0 && event.type < eventType_pool.size())
            eventType_pool[event.type](event);

        if (clock::now() - last_save >= std::chrono::seconds(60)) // @note so a crash or force stop loses at most a minute
        {
            save_everything();
            last_save = clock::now();
        }
    }

    safe_disconnect_peers(gSignal);
    save_everything();
    worlds.clear(); // @note before the database connection closes
    enet_host_destroy(host);
    if (host != nullptr) host = nullptr;
    enet_deinitialize();
    mysql_close(db.mysql);
    db.mysql = nullptr;
    mysql_library_end();

    print("killed gurotopia! [EXIT_SUCCESS]");
    return EXIT_SUCCESS;
}
