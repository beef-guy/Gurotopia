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
#include "include/database/platform.hpp"
#include "include/action/fishing.hpp"
#include "include/action/automation.hpp"
#include "include/action/radiation.hpp"
#include "include/action/transform.hpp"
#include "include/action/fossils.hpp"
#include "include/action/ghosts.hpp"
#include "include/action/steam.hpp"
#include "include/action/special_items.hpp"
#include "include/action/carnival.hpp"
#include "include/commands/renderworld.hpp"
#include "include/render/render.hpp"
#include "include/render/discord.hpp"

#include <algorithm>
#include <cctype>
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

static void print_help()
{
    print(
        "usage: main.exe [options]\n"
        "  --port N            game port (default: the port in server_data.php, 17091)\n"
        "  --database NAME     MariaDB database to use (default: gurotopia)\n"
        "  --server-data FILE  server settings file (default: server_data.php)\n"
        "  --mysql-login FILE  MariaDB login file (default: mysql_login.txt)\n"
        "  --max-players N     most players online at once (default: 50)\n"
        "  --https-port N      port for the game's login request (default: 443)\n"
        "  --https-bind IP     address the login listener binds to (default: 0.0.0.0, every address)\n"
        "  --no-https          don't answer the login request. use for a 2nd server on the same PC\n"
        "  --owner NAME        this account is always a developer (staff commands, /find, /give)\n"
        "  --sandbox           let every player use /find (a creative server)\n"
        "  --textures DIR      Growtopia's game folder, for /renderworld (default: %LOCALAPPDATA%\\Growtopia\\game)\n"
        "  --discord-webhook F file with the Discord webhook URL /renderworld posts to (default: discord_webhook.txt)\n"
        "\n"
        "  many servers behind one IP:\n"
        "  --platform-db NAME  database that lists the servers (e.g. gurotopia_platform)\n"
        "  --gateway           only handle logins, then send each player to the server they typed\n"
        "  --default-server N  gateway: where to send players who typed no server name\n"
        "  --name NAME         this server's name in the platform database (what players type)\n"
        "  --help              show this"
    );
}

static std::string discord_webhook_file{"discord_webhook.txt"};

/* @note returns false when the options are wrong, after printing why */
static bool parse_options(int argc, char *argv[])
{
    const auto number = [](const std::string &text, int low, int high, int &out) -> bool
    {
        try {
            std::size_t used = 0;
            out = std::stoi(text, &used);
            return used == text.size() && out >= low && out <= high;
        }
        catch (const std::logic_error &) { return false; }
    };
    const auto database_name = [](const std::string &name) -> bool
    {
        return !name.empty() && name.size() <= 64 &&
            std::ranges::all_of(name, [](unsigned char c) { return std::isalnum(c) || c == '_'; });
    };
    for (int i = 1; i < argc; ++i)
    {
        const std::string option = argv[i];
        if (option == "--help" || option == "-h")
        {
            print_help();
            std::exit(EXIT_SUCCESS);
        }
        if (option == "--no-https")
        {
            gServer_data.https_port = 0;
            continue;
        }
        if (option == "--sandbox")
        {
            gServer_data.sandbox = true;
            continue;
        }
        if (option == "--gateway")
        {
            platform::gateway = true;
            continue;
        }
        if (i + 1 >= argc)
        {
            print(std::format("{} needs a value. run with --help to see the options.", option));
            return false;
        }
        const std::string value = argv[++i];
        int n = 0;

        if (option == "--port")
        {
            if (!number(value, 1, 65535, n)) { print(std::format("--port must be 1 to 65535, not {}", value)); return false; }
            gServer_data.port_override = n;
        }
        else if (option == "--https-port")
        {
            if (!number(value, 0, 65535, n)) { print(std::format("--https-port must be 0 to 65535, not {}", value)); return false; }
            gServer_data.https_port = static_cast<u_short>(n);
        }
        else if (option == "--max-players")
        {
            if (!number(value, 1, 4095, n)) { print(std::format("--max-players must be 1 to 4095, not {}", value)); return false; } // @note ENet's limit
            gServer_data.max_players = static_cast<std::size_t>(n);
        }
        else if (option == "--database")
        {
            if (!database_name(value)) { print("--database may only use letters, digits and _ (at most 64)"); return false; }
            db.database = value;
        }
        else if (option == "--platform-db")
        {
            if (!database_name(value)) { print("--platform-db may only use letters, digits and _ (at most 64)"); return false; }
            platform::database = value;
        }
        else if (option == "--name" || option == "--default-server")
        {
            if (!platform::valid_name(value)) { print(std::format("{} may only use letters and digits (at most 24)", option)); return false; }
            (option == "--name" ? platform::server_name : platform::default_server) = value;
        }
        else if (option == "--owner")
        {
            if (value.empty() || value.size() > 18 || !isalnum(value)) { print("--owner must be a GrowID: letters and digits, at most 18"); return false; }
            gServer_data.owner = value;
        }
        else if (option == "--https-bind") gServer_data.https_bind = value;
        else if (option == "--server-data") gServer_data.config_file = value;
        else if (option == "--mysql-login") db.login_file = value;
        else if (option == "--textures") render::texture_folder = value;
        else if (option == "--discord-webhook") discord_webhook_file = value;
        else
        {
            print(std::format("unknown option {}. run with --help to see the options.", option));
            return false;
        }
    }
    if ((platform::gateway || !platform::server_name.empty()) && !platform::enabled())
    {
        print("--gateway and --name need --platform-db too");
        return false;
    }
    if (platform::gateway && !platform::server_name.empty())
    {
        print("a gateway has no --name, it sends players to the named servers");
        return false;
    }
    if (platform::gateway) db.database = platform::database; // @note the gateway only reads the server list
    return true;
}

/* @note how many players are logged in here */
static int count_online()
{
    int online = 0;
    peers("", PeerSearch::EVERYONE, [&online](ENetPeer *peer)
    {
        const ::peer *pPeer = static_cast<::peer*>(peer->data);
        if (pPeer != nullptr && pPeer->authenticated) ++online;
    });
    return online;
}

int main(int argc, char *argv[])
{
    if (!parse_options(argc, argv))
    {
        fflush(stdout);
        return EXIT_FAILURE;
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler); // @note systemd, docker and kill stop the server cleanly too
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
            gServer_data.init(); // @note ./server_data.php, or --server-data
            if (gServer_data.https_port != 0) std::thread(&https::listener).detach();
            else print("not answering the login request (--no-https). another server must do that.");

            ENetAddress address{
                .type = ENET_ADDRESS_TYPE_IPV4,
                .port = gServer_data.port
            };
            host = enet_host_create (ENET_ADDRESS_TYPE_IPV4, &address, gServer_data.max_players, 2ull, 0u, 0u);
        } // @note delete address
        if (host == nullptr) throw std::runtime_error(std::format("could not open port {}. is the server already running?", gServer_data.port));

        print(std::format("game port {}, up to {} players", gServer_data.port, gServer_data.max_players));

        host->usingNewPacketForServer = true;
        host->checksum = enet_crc32;
        enet_host_compress_with_range_coder(host);

        db.load_login(); // @note ./mysql_login.txt, or --mysql-login
        db.mysql_connect();

        decode_items_dat();
        discord::load_webhook(discord_webhook_file); // @note ./discord_webhook.txt, or --discord-webhook
        parse_store();
        check_for_holiday();

        if (platform::gateway) print(std::format("gateway mode: sending players to the servers listed in {}.servers", platform::database));
        else if (!platform::server_name.empty())
        {
            platform::report("running", 0);
            print(std::format("running as server {} in {}", platform::server_name, platform::database));
        }
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
        if (enet_host_service(host, &event, 100/*ms*/) > 0 && event.type < eventType_pool.size())
            eventType_pool[event.type](event);
        fishing_tick(); // @note fish bites
        automation_tick(); // @note Tesseract Manipulator, Heart of Gaia, Techno-Organic Engine
        radiation_tick(); // @note Geiger Counters, the Irradiated mod, the Great Chameleon
        transform_tick(); // @note /transformscan
        fossil_tick(); // @note rocks in busy worlds turn into Fossil Rocks
        ghost_tick(); // @note ghosts float about, Ghost Jars catch them, new ghosts move in
        steam_tick(); // @note steam moves through Steam Blocks, Steam Engines puff
        special_tick(); // @note Freeze and Curse Wands wearing off, Challenge Timers, Game Generator games
        carnival_tick(); // @note Carnival games: timers, targets, platforms
        render_tick(); // @note /renderworld results

        if (clock::now() - last_save >= std::chrono::seconds(60)) // @note so a crash or force stop loses at most a minute
        {
            save_everything();
            platform::report("running", count_online()); // @note also tells the gateway this server is still alive
            last_save = clock::now();
        }
    }

    safe_disconnect_peers(gSignal);
    save_everything();
    platform::report("stopped", 0);
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
