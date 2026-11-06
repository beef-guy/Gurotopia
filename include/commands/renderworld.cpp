#include "pch.hpp"

#include "onVariant/ConsoleMessage.hpp"
#include "render/render.hpp"
#include "render/discord.hpp"
#include "tools/terminal.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "renderworld.hpp"

namespace
{
    using clock = std::chrono::steady_clock;

    std::atomic<bool> busy{false}; // @note one render at a time: a full world is 3200 x 1920 pixels
    std::unordered_map<int, clock::time_point> last_render{}; // @note user id: when they last rendered (main thread only)
    constexpr auto COOLDOWN = std::chrono::seconds(60);

    std::mutex done_mutex;
    std::vector<std::pair<int, std::string>> done{}; // @note user id, message: filled by the render thread, sent by render_tick()

    void finish(int uid, std::string message)
    {
        std::lock_guard lock(done_mutex);
        done.emplace_back(uid, std::move(message));
    }
}

void renderworld(ENetEvent &event, const std::string_view)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
    if (pPeer->netid == 0 || world == worlds.end()) return on::ConsoleMessage(event.peer, "Enter a world first.");

    const clock::time_point now = clock::now();
    if (auto it = last_render.find(pPeer->user_id); it != last_render.end() && now - it->second < COOLDOWN && pPeer->role < DEVELOPER)
    {
        const auto wait = std::chrono::duration_cast<std::chrono::seconds>(COOLDOWN - (now - it->second)).count();
        return on::ConsoleMessage(event.peer, std::format("`4Wait {} more second{} before rendering again.``", wait, wait == 1 ? "" : "s"));
    }
    if (busy.exchange(true)) return on::ConsoleMessage(event.peer, "`4Another world is being rendered right now. Try again in a few seconds.``");
    last_render[pPeer->user_id] = now;

    on::ConsoleMessage(event.peer, std::format("`oRendering `w{}``...``", world->name));

    render::snapshot shot = render::take(*world); // @note copied here, so the world can change while it draws
    std::thread([shot = std::move(shot), uid = pPeer->user_id, growid = pPeer->growid, webhook = discord::webhook]()
    {
        std::string error{};
        const std::vector<u_char> png = render::draw(shot, error);
        if (png.empty())
        {
            print(std::format("/renderworld {} failed: {}", shot.name, error));
            finish(uid, std::format("`4Render failed:`` {}", error));
            busy = false;
            return;
        }

        std::error_code ec{};
        std::filesystem::create_directories("renders", ec);
        const std::string file = std::format("renders/{}.png", shot.name);
        std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));

        if (webhook.empty())
        {
            finish(uid, std::format("`2Rendered!`` Saved as {} on the server. (to post renders to Discord, put a webhook URL in discord_webhook.txt and restart)", file));
            busy = false;
            return;
        }
        const std::string message = std::format("**{}** rendered by **{}**", shot.name, growid);
        const std::string sent = discord::post_image(webhook, message, std::format("{}.png", shot.name), png);
        if (sent.empty())
        {
            print(std::format("/renderworld: posted {} to Discord", shot.name));
            finish(uid, std::format("`2Rendered `w{}`` and posted it to Discord!``", shot.name));
        }
        else
        {
            print(std::format("/renderworld: Discord post failed: {}", sent));
            finish(uid, std::format("`4Rendered, but posting to Discord failed:`` {}. The picture is saved as {} on the server.", sent, file));
        }
        busy = false;
    }).detach();
}

void render_tick()
{
    std::vector<std::pair<int, std::string>> ready{};
    {
        std::lock_guard lock(done_mutex);
        if (done.empty()) return;
        ready.swap(done);
    }
    for (const auto &[uid, message] : ready)
        peers("", PeerSearch::EVERYONE, [&](ENetPeer *peer)
        {
            const ::peer *pPeer = static_cast<::peer*>(peer->data);
            if (pPeer != nullptr && pPeer->user_id == uid) on::ConsoleMessage(peer, message);
        });
}
