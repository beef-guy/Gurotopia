#include "pch.hpp"

#include "onVariant/ConsoleMessage.hpp"
#include "tools/create_dialog.hpp"
#include "tools/random.hpp"
#include "tools/sound.hpp"
#include "tools/time.hpp"

#include "fishing.hpp"

#include <algorithm>
#include <chrono>
#include <map>

/*
* @note fishing follows real Growtopia: rod in hand, bait used on the water in front of your feet, wait for the splash, punch.
* packets: GONE_FISHIN 0x1f (gtps-guide packets/1f-gone-fishin.md), the catch flies to the player with ITEM_EFFECT 0x13
* and the bite splashes with particle 34. fish sizes from github.com/cppdecl/Nomikai Src/Misc/Constants.hh
*/

namespace
{
    constexpr short FISHING_HAT = 3042;
    constexpr short EZ_ROD = 3008, LICORICE_ROD = 3010, GOLDENROD = 3040;
    constexpr short URANIUM_BLOCK = 4658, ICE_CRUST_BLOCK = 5602;
    constexpr short FISH_TANK = 3004;

    const std::map<short, int> max_size = {
        {3000, 15}, {3026, 30}, {3030, 50}, {3038, 70}, {3032, 90}, {3034, 120}, {3036, 160}, {3814, 160}, {4958, 200}, {3024, 200}, {7744, 200}, // @note regular
        {3456, 100}, {3450, 150}, {3458, 50}, {3454, 200}, {3452, 100}, {3460, 200}, // @note crabs and lobsters
        {5538, 160}, {5542, 60}, {5548, 60}, // @note radioactive water
        {5580, 190}, {5574, 190}, {5552, 120}, // @note ice water
        {3094, 200}, {3092, 200}, {3096, 200}, // @note otherworldly
        {3226, 180}, {3224, 150}, {3220, 200}, {3222, 120}, // @note winterfest
        {5448, 200}, {5450, 50},
        {3434, 150}, {3436, 150}, {3550, 150}, {3620, 150}, {3746, 150}, {5100, 150}, {10264, 150}, {3440, 150}, {3820, 150}, {3438, 150}, {3544, 150}, // @note catch of the day
    };
    const std::vector<short> regular_fish = {3000, 3026, 3030, 3038, 3032, 3034, 3036, 3814, 4958, 3024, 7744};
    const std::vector<short> crabs = {3456, 3450, 3458, 3454, 3452, 3460};
    const std::vector<short> radioactive_fish = {5538, 5542, 5548};
    const std::vector<short> ice_fish = {5580, 5574, 5552};
    const std::vector<short> otherworldly_fish = {3094, 3092, 3096};
    const std::vector<short> winter_fish = {3226, 3224, 3220, 3222};
    const std::vector<short> catch_of_the_day = {3434, 3436, 3550, 3620, 3746, 5100, 10264, 3440, 3820, 3438, 3544};

    /* @return the biggest fish (lbs) this bait reaches in normal water, 0 = not normal bait */
    int bait_reach(short bait)
    {
        switch (bait)
        {
            case 2914: return 30;   // @note Wiggly Worm: only the smallest fish
            case 3012: return 70;   // @note Shiny Flashy Thing
            case 3014: return 120;  // @note Salmon Eggs: mid-sized
            case 3016: return 160;  // @note Fishing Fly: even large fish
            case 3018: return 200;  // @note Shrimp Lure: no fish too big
            case 3020: return 200;  // @note Whizmo Gizmo: everything, and the biggest
        }
        return 0;
    }
    bool is_bait(short id)
    {
        return bait_reach(id) > 0 || id == 3098 || id == 4246 || id == 4248 || id == 3218 || id == 3432 || id == 5526 || id == 5528;
    }
    bool is_rod(short id)
    {
        return id == 2912 || id == EZ_ROD || id == LICORICE_ROD || id == GOLDENROD || id == 3100 || id == 5740 || id == 10262;
    }

    long long now_ms()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    enum class water : u_char { NONE, NORMAL, RADIOACTIVE, ICE };
    water water_at(const ::world &world, const ::pos &tile)
    {
        if (!valid_tile(world, tile)) return water::NONE;
        const ::block &block = world.blocks[cord(tile.x_int(), tile.y_int())];
        if (block.fg == URANIUM_BLOCK) return water::RADIOACTIVE;
        if (block.fg == ICE_CRUST_BLOCK) return water::ICE;
        if ((block.state[3] & S_WATER) && (block.fg == 0 || id_to_item(block.fg).collision != collision::FULL)) return water::NORMAL;
        return water::NONE;
    }

    /* @brief GONE_FISHIN: start (0) or stop (2) the fishing pose for everyone in the world */
    void gone_fishin(const std::string &world, int netid, const ::pos &tile, int mode)
    {
        ::gamePacket packet{.type = 0x1f | (mode << 16), .netid = netid, .punch = tile};
        ::blob blob = compress_state(packet);
        peers(world, PeerSearch::WORLD, [&blob](ENetPeer *peer) { send_data(*peer, blob); });
    }

    void stop(ENetPeer *peer, ::peer *pPeer)
    {
        if (!pPeer->fishing.active) return;
        gone_fishin(pPeer->fishing.world, pPeer->netid, pPeer->fishing.tile, 2);
        pPeer->fishing = {};
        (void)peer;
    }

    void say(ENetPeer *peer, ::peer *pPeer, const std::string &text)
    {
        send_varlist(peer, { "OnTalkBubble", pPeer->netid, text, 0u, 1u });
        on::ConsoleMessage(peer, text);
    }

    short pick(const std::vector<short> &list)
    {
        return list[RandomRange(0, static_cast<int>(list.size()))]; // @note RandomRange's top is exclusive
    }

    std::string quality(short fish, int lbs)
    {
        const int max = fish_max_size(fish);
        if (lbs > max) return "Massive";
        if (lbs * 10 >= max * 9) return "Perfect";
        return "";
    }

    /* @brief what the bait brings up. fish = 0: nothing, the reason is in `message` */
    struct catch_t { short item{}; int lbs{}; std::string message{}; };
    catch_t roll_catch(short bait, water kind, short rod)
    {
        catch_t c{};
        auto sized = [&](short fish, int cap)
        {
            const int max = std::min(fish_max_size(fish), cap);
            int lbs = RandomRange(1, max + 1);
            if (bait == 3020) lbs = std::max(lbs, RandomRange(1, max + 1)); // @note Whizmo Gizmo homes in on the largest fish
            if (rod == GOLDENROD) lbs = lbs * 5 / 4; // @note 25% bigger fish
            return std::max(1, lbs);
        };
        if (kind == water::RADIOACTIVE || kind == water::ICE)
        {
            const bool ice = kind == water::ICE;
            if (bait == (ice ? 5528 : 5526))
            {
                c.item = pick(ice ? ice_fish : radioactive_fish);
                c.lbs = sized(c.item, 200);
                return c;
            }
            const int reach = bait_reach(bait);
            const int size = (reach <= 70) ? 0 : (reach <= 120) ? 1 : 2;
            c.item = static_cast<short>((ice ? 5626 : 5632) + size * 2); // @note tiny, small, large chunk
            c.lbs = 1;
            c.message = ice ?
                std::format("Oh no! The frigid waters froze your bait into a `2{}``!", id_to_item(c.item).raw_name) :
                std::format("Yikes! The radioactive waters blasted your bait into a `2{}``!", id_to_item(c.item).raw_name);
            return c;
        }
        switch (bait)
        {
            case 3098: case 4246: case 4248: // @note Otherworldly Bait: things from a dark dimension
            {
                c.item = pick(otherworldly_fish);
                c.lbs = sized(c.item, (bait == 4246) ? 60 : (bait == 4248) ? 120 : 200);
                return c;
            }
            case 3218: // @note Candy Cane Bait: seasonal fish
            {
                c.item = pick(winter_fish);
                c.lbs = sized(c.item, 200);
                return c;
            }
            case 3432: // @note Catch-of-the-Day Bait: one fish a day, and it can escape
            {
                if (RandomRange(0, 10) < 3) { c.message = "The fish got away!"; return c; }
                const std::time_t day = std::time(nullptr) / 86400;
                c.item = catch_of_the_day[static_cast<std::size_t>(day) % catch_of_the_day.size()];
                c.lbs = sized(c.item, 200);
                return c;
            }
        }
        const int reach = bait_reach(bait);
        std::vector<short> can{};
        for (const short fish : regular_fish) if (fish_max_size(fish) <= reach || reach >= 200) can.push_back(fish);
        if (can.empty()) can.push_back(3000);
        c.item = pick(can);
        c.lbs = sized(c.item, reach);
        return c;
    }

    void reel_in(ENetEvent &event, ::world &world, ::peer *pPeer)
    {
        const short bait = pPeer->fishing.bait;
        const ::pos tile = pPeer->fishing.tile;
        const long long bite = pPeer->fishing.bite;
        const long long window = (pPeer->clothing[clothing::HAND] == EZ_ROD) ? 4000 : 2000; // @note the E-Z Rod keeps fish on the line twice as long
        stop(event.peer, pPeer);

        const bool keep_bait = pPeer->clothing[clothing::HEAD] == FISHING_HAT && RandomRange(0, 100) < 5;
        if (pPeer->have_slot(::slot(bait, 0)) <= 0) return;
        if (!keep_bait) modify_item_inventory(event, ::slot(bait, -1));

        if (bite == 0 || now_ms() - bite > window)
        {
            say(event.peer, pPeer, "There was nothing on the line!");
            return;
        }
        const catch_t c = roll_catch(bait, water_at(world, tile), pPeer->clothing[clothing::HAND]);
        if (c.item == 0)
        {
            say(event.peer, pPeer, c.message);
            return;
        }
        if (!pPeer->can_hold(c.item, c.lbs))
        {
            say(event.peer, pPeer, "You don't have room in your backpack for that catch!");
            return;
        }
        modify_item_inventory(event, ::slot(c.item, static_cast<short>(c.lbs)));

        ::gamePacket effect{ // @note the catch flies out of the water to the player
            .type  = 0x13 | (5 << 24),
            .uid   = pPeer->netid,
            .delay = 0,
            .pos   = ::pos{tile.x * 32.0f + RandomRange(0, 20), tile.y * 32.0f + RandomRange(0, 15)},
            .punch = ::pos{static_cast<int>(c.item), c.lbs}
        };
        ::blob blob = compress_state(effect);
        peers(world.name, PeerSearch::WORLD, [&blob](ENetPeer *peer) { send_data(*peer, blob); });
        play_in_world(world.name, pPeer->netid, "audio/teleport.wav");

        if (!c.message.empty()) { say(event.peer, pPeer, c.message); return; }
        const std::string q = quality(c.item, c.lbs);
        say(event.peer, pPeer, std::format("You caught a {}`2{} lb. {}``!", q.empty() ? "" : std::format("`9{}`` ", q), c.lbs, id_to_item(c.item).raw_name));
        pPeer->add_xp(event, 1 + c.lbs / 20);
    }

    ::world *world_of(::peer *pPeer)
    {
        if (pPeer->netid == 0) return nullptr;
        auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
        return (world != worlds.end()) ? &*world : nullptr;
    }

    bool read_pos(const ::hPipe &hPipe, const ::world &world, u_char block_type, ::pos &pos)
    {
        pos = ::pos{atoi(hPipe["tilex"].c_str()), atoi(hPipe["tiley"].c_str())};
        if (!valid_tile(world, pos)) return false;
        return id_to_item(world.blocks[cord(pos.x_int(), pos.y_int())].fg).type == block_type;
    }

    void update_tile(ENetEvent &event, ::world &world, const ::pos &pos)
    {
        ::block &block = world.blocks[cord(pos.x_int(), pos.y_int())];
        send_tile_update(event, ::gamePacket{.id = block.fg, .punch = pos}, block, world);
    }

    void send(ENetEvent &event, const ::pos &pos, ::create_dialog &dialog, const std::string &name, const std::string &close, const std::string &ok)
    {
        dialog.embed_data("tilex", pos.x_int())
              .embed_data("tiley", pos.y_int());
        send_varlist(event.peer, { "OnDialogRequest", dialog.end_dialog(name, close, ok) });
    }

    bool is_crab(short id) { return std::ranges::find(crabs, id) != crabs.end(); }
}

int fish_max_size(short fish)
{
    auto it = max_size.find(fish);
    return (it != max_size.end()) ? it->second : 150;
}

bool fishing_tile_change(ENetEvent &event, ::world &world, const ::gamePacket &gamePacket)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    if (pPeer->fishing.active)
    {
        if (pPeer->fishing.world != world.name) { pPeer->fishing = {}; return false; }
        if (gamePacket.id == 18 || gamePacket.id == pPeer->fishing.bait) // @note punching (or using the bait again) reels in
        {
            reel_in(event, world, pPeer);
            return true;
        }
        return false;
    }
    if (!is_bait(static_cast<short>(gamePacket.id))) return false;

    const ::pos standing = pPeer->pos.by_32(true);
    const ::pos target = gamePacket.punch;
    if (water_at(world, target) == water::NONE) return false; // @note not water: the bait does nothing here

    if (!is_rod(pPeer->clothing[clothing::HAND])) throw std::runtime_error("You'll need a fishing rod to fish!");
    if (water_at(world, standing) != water::NONE) throw std::runtime_error("You're scaring the fish by being in the water!");
    const ::pos front{standing.x_int() + (pPeer->facing_left ? -1 : 1), standing.y_int()};
    if (water_at(world, front) != water::NONE) throw std::runtime_error("You're halfway underwater! You can only fish from shore.");
    const ::pos below{front.x_int(), front.y_int() + 1};
    const ::pos below_back{standing.x_int() + (pPeer->facing_left ? 1 : -1), standing.y_int() + 1}; // @note the client may face either way
    if (target != below && target != below_back) throw std::runtime_error("You can only fish in the water at your feet in front of you.");

    const water kind = water_at(world, target);
    const short bait = static_cast<short>(gamePacket.id);
    if (bait == 5526 && kind != water::RADIOACTIVE) throw std::runtime_error("This bait only works in radioactive water!");
    if (bait == 5528 && kind != water::ICE) throw std::runtime_error("This bait only works in icy water!");

    pPeer->fishing = {};
    pPeer->fishing.active = true;
    pPeer->fishing.world = world.name;
    pPeer->fishing.tile = target;
    pPeer->fishing.start = pPeer->pos;
    pPeer->fishing.bait = bait;
    const int wait = RandomRange(4000, 15000) / ((pPeer->clothing[clothing::HAND] == LICORICE_ROD) ? 2 : 1); // @note the Licorice Rod gets bites twice as often
    pPeer->fishing.next_bite = now_ms() + wait;
    gone_fishin(world.name, pPeer->netid, target, 0);
    return true;
}

void fishing_moved(ENetEvent &event)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (!pPeer->fishing.active) return;
    if (std::abs(pPeer->pos.x - pPeer->fishing.start.x) < 6.0f && std::abs(pPeer->pos.y - pPeer->fishing.start.y) < 6.0f) return;
    stop(event.peer, pPeer);
    say(event.peer, pPeer, "Sit still if you wanna fish!");
}

void fishing_tick()
{
    const long long now = now_ms();
    peers("", PeerSearch::EVERYONE, [now](ENetPeer *peer)
    {
        ::peer *pPeer = static_cast<::peer*>(peer->data);
        if (pPeer == nullptr || !pPeer->fishing.active) return;
        if (pPeer->netid == 0 || pPeer->recent_worlds.back() != pPeer->fishing.world) { pPeer->fishing = {}; return; }
        if (now < pPeer->fishing.next_bite) return;

        pPeer->fishing.bite = now; // @note a bite! punch now
        const int wait = RandomRange(4000, 15000) / ((pPeer->clothing[clothing::HAND] == LICORICE_ROD) ? 2 : 1);
        pPeer->fishing.next_bite = now + wait;
        play_in_world(pPeer->fishing.world, pPeer->netid, "audio/splash.wav");
        ::gamePacket splash{
            .type = 0x11, // @note PACKET_SEND_PARTICLE_EFFECT, water splash
            .pos = ::pos{pPeer->fishing.tile.x * 32.0f + RandomRange(0, 20), pPeer->fishing.tile.y * 32.0f + RandomRange(0, 15)},
            .speed = ::pos{0.0f, 34.0f},
            .idk = 270.0f
        };
        ::blob blob = compress_state(splash);
        peers(pPeer->fishing.world, PeerSearch::WORLD, [&blob](ENetPeer *p) { send_data(*p, blob); });
    });
}

/* ---------------- Fish Tank Port ---------------- */

namespace
{
    /* @return how many Fish Tank blocks this port's tank is made of */
    int tank_size(const ::world &world, const ::pos &port)
    {
        const int height = static_cast<int>(world.blocks.size()) / 100;
        std::vector<bool> seen(world.blocks.size(), false);
        std::vector<int> queue{cord(port.x_int(), port.y_int())};
        seen[queue[0]] = true;
        int size = 0;
        for (std::size_t head = 0; head < queue.size() && size < 1000; ++head)
        {
            const int at = queue[head];
            if (world.blocks[at].bg != FISH_TANK) continue;
            ++size;
            const int ax = at % 100, ay = at / 100;
            const int next[4][2] = {{ax, ay - 1}, {ax - 1, ay}, {ax + 1, ay}, {ax, ay + 1}};
            for (const auto &n : next)
            {
                if (n[0] < 0 || n[0] >= 100 || n[1] < 0 || n[1] >= height) continue;
                const int i = cord(n[0], n[1]);
                if (seen[i]) continue;
                seen[i] = true;
                queue.push_back(i);
            }
        }
        return size;
    }
    int tank_capacity(const ::world &world, const ::pos &port)
    {
        return std::clamp(tank_size(world, port) / 3, 1, 20); // @note one fish for every three Fish Tank blocks
    }
}

void fish_tank_wrench(ENetEvent &event, ::world &world, const ::pos &pos)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const ::item &port = id_to_item(world.blocks[cord(pos.x_int(), pos.y_int())].fg);
    auto tank = std::ranges::find(world.fish_tanks, pos, &::fish_tank::pos);
    const std::vector<std::pair<short, int>> fish = (tank != world.fish_tanks.end()) ? tank->fish : std::vector<std::pair<short, int>>{};
    const bool owner = world.can_edit(*pPeer, pos, false);
    const int capacity = tank_capacity(world, pos);

    ::create_dialog dialog = ::create_dialog()
        .set_default_color("`o")
        .add_label_with_icon("big", std::format("`w{}``", port.raw_name), port.id)
        .add_spacer("small")
        .add_textbox(std::format("`oThis tank holds `w{}/{}`` fish.``", fish.size(), capacity));
    for (std::size_t i = 0; i < fish.size(); ++i)
    {
        const std::string text = std::format("`w{} lb. {}``", fish[i].second, id_to_item(fish[i].first).raw_name);
        if (owner) dialog.add_raw(std::format("add_label_with_icon_button|small|{} `9(remove)``|left|{}|remove_{}|\n", text, fish[i].first, i));
        else dialog.add_label_with_icon("small", text, fish[i].first);
    }
    if (owner)
    {
        dialog.add_spacer("small");
        if (static_cast<int>(fish.size()) < capacity) dialog.add_item_picker("add_fish", "`wAdd a fish``", "Choose a fish to put in the tank");
        send(event, pos, dialog, "fish_tank", "Close", "");
        return;
    }
    send(event, pos, dialog, "fish_tank", "Close", "");
}

void fish_tank_edit(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::world *world = world_of(pPeer);
    ::pos pos{};
    if (world == nullptr || !read_pos(hPipe, *world, type::FISH_TANK_PORT, pos) || !world->can_edit(*pPeer, pos, false)) return;
    auto found = std::ranges::find(world->fish_tanks, pos, &::fish_tank::pos);
    if (found == world->fish_tanks.end()) found = world->fish_tanks.emplace(world->fish_tanks.end(), ::fish_tank{.pos = pos});
    ::fish_tank &tank = *found;
    const std::string button = hPipe["buttonClicked"];

    if (const short id = static_cast<short>(atoi(hPipe["add_fish"].c_str())); id > 0)
    {
        if (id_to_item(id).type != type::FISH) throw std::runtime_error("Only fish can go in a fish tank!");
        const int have = pPeer->have_slot(::slot(id, 0));
        if (have <= 0) return;
        ::create_dialog dialog = ::create_dialog()
            .set_default_color("`o")
            .add_label_with_icon("big", std::format("`w{}``", id_to_item(id).raw_name), id)
            .add_textbox(std::format("`oYou have `w{}`` lb. of {}. How big is the fish you're putting in?``", have, id_to_item(id).raw_name))
            .add_text_input("lbs", "Pounds:", std::to_string(std::min(have, fish_max_size(id))), 3)
            .embed_data("fish", id);
        send(event, pos, dialog, "fish_tank", "Cancel", "Put it in");
        return;
    }
    if (const short id = static_cast<short>(atoi(hPipe["fish"].c_str())); id > 0 && !hPipe["lbs"].empty())
    {
        if (id_to_item(id).type != type::FISH) return;
        if (static_cast<int>(tank.fish.size()) >= tank_capacity(*world, pos)) throw std::runtime_error("This tank is full! Build it bigger to fit more fish.");
        const int lbs = std::clamp(atoi(hPipe["lbs"].c_str()), 1, static_cast<int>(pPeer->have_slot(::slot(id, 0))));
        if (pPeer->have_slot(::slot(id, 0)) < lbs || lbs < 1) return;
        modify_item_inventory(event, ::slot(id, static_cast<short>(-lbs)));
        tank.fish.emplace_back(id, lbs);
        say(event.peer, pPeer, std::format("You put a `2{} lb. {}`` in the tank!", lbs, id_to_item(id).raw_name));
    }
    else if (button.starts_with("remove_"))
    {
        const std::size_t i = static_cast<std::size_t>(atoi(button.c_str() + 7));
        if (i >= tank.fish.size()) return;
        const auto [id, lbs] = tank.fish[i];
        if (!pPeer->can_hold(id, lbs)) throw std::runtime_error("You don't have room in your backpack!");
        modify_item_inventory(event, ::slot(id, static_cast<short>(lbs)));
        tank.fish.erase(tank.fish.begin() + static_cast<long>(i));
        say(event.peer, pPeer, std::format("You took a `2{} lb. {}`` out of the tank.", lbs, id_to_item(id).raw_name));
    }
    else return;
    update_tile(event, *world, pos);
    play_in_world(world->name, pPeer->netid, "audio/splash.wav");
    pPeer->mysql_update_all();
    world->save();
}

/* ---------------- Fish Wall Mount ---------------- */

void fish_mount_wrench(ENetEvent &event, ::world &world, const ::pos &pos)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const ::item &plaque = id_to_item(world.blocks[cord(pos.x_int(), pos.y_int())].fg);
    auto mount = std::ranges::find(world.fish_mounts, pos, &::fish_mount::pos);

    ::create_dialog dialog = ::create_dialog()
        .set_default_color("`o")
        .add_label_with_icon("big", std::format("`w{}``", plaque.raw_name), plaque.id)
        .add_spacer("small");
    if (mount != world.fish_mounts.end() && mount->fish != 0)
    {
        dialog.add_label_with_icon("small", std::format("`w{}``", mount->label), mount->fish);
        send(event, pos, dialog, "fish_mount", "Close", "");
        return;
    }
    if (!world.can_edit(*pPeer, pos, false))
    {
        dialog.add_textbox("`oNothing is mounted here yet.``");
        send(event, pos, dialog, "fish_mount", "Close", "");
        return;
    }
    dialog.add_textbox(plaque.id == 6328 ? "`oOnly `9Perfect`` or `9Massive`` crabs and lobsters can go on this mount. Once mounted, it can never be removed!``" :
                                           "`oOnly `9Perfect`` or `9Massive`` fish can go on this mount. Once mounted, it can never be removed!``")
          .add_item_picker("mount_fish", "`wChoose a catch``", "Choose a fish to mount");
    send(event, pos, dialog, "fish_mount", "Close", "");
}

void fish_mount_edit(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::world *world = world_of(pPeer);
    ::pos pos{};
    if (world == nullptr || !read_pos(hPipe, *world, type::FISH_MOUNT, pos) || !world->can_edit(*pPeer, pos, false)) return;
    const short plaque = world->blocks[cord(pos.x_int(), pos.y_int())].fg;
    auto found = std::ranges::find(world->fish_mounts, pos, &::fish_mount::pos);
    if (found != world->fish_mounts.end() && found->fish != 0) return; // @note mounted for good

    auto check = [&](short id)
    {
        if (id_to_item(id).type != type::FISH) throw std::runtime_error("That's not a fish!");
        if (plaque == 6328 && !is_crab(id)) throw std::runtime_error("Only crabs and lobsters go on this mount!");
        if (plaque != 6328 && is_crab(id)) throw std::runtime_error("Crabs and lobsters go on a Crab Wall Mount!");
    };
    if (const short id = static_cast<short>(atoi(hPipe["mount_fish"].c_str())); id > 0)
    {
        check(id);
        const int have = pPeer->have_slot(::slot(id, 0));
        if (have <= 0) return;
        ::create_dialog dialog = ::create_dialog()
            .set_default_color("`o")
            .add_label_with_icon("big", std::format("`w{}``", id_to_item(id).raw_name), id)
            .add_textbox(std::format("`oYou have `w{}`` lb. of {}. A `9Perfect`` one weighs at least `w{}`` lb.``", have, id_to_item(id).raw_name, (fish_max_size(id) * 9 + 9) / 10))
            .add_text_input("lbs", "Pounds:", std::to_string(std::min({have, 255, fish_max_size(id)})), 3)
            .embed_data("fish", id);
        send(event, pos, dialog, "fish_mount", "Cancel", "Mount it");
        return;
    }
    const short id = static_cast<short>(atoi(hPipe["fish"].c_str()));
    if (id <= 0 || hPipe["lbs"].empty()) return;
    check(id);
    const int lbs = std::clamp(atoi(hPipe["lbs"].c_str()), 1, 255);
    if (pPeer->have_slot(::slot(id, 0)) < lbs) throw std::runtime_error("You don't have that much fish!");
    const std::string q = quality(id, lbs);
    if (q.empty()) throw std::runtime_error("Only `9Perfect`` or `9Massive`` catches can be mounted!");
    modify_item_inventory(event, ::slot(id, static_cast<short>(-lbs)));
    if (found == world->fish_mounts.end()) found = world->fish_mounts.emplace(world->fish_mounts.end(), ::fish_mount{.pos = pos});
    found->fish = id;
    found->lbs = lbs;
    found->label = std::format("{}'s {} {} lb. {}", pPeer->growid, q, lbs, id_to_item(id).raw_name);
    update_tile(event, *world, pos);
    play_in_world(world->name, pPeer->netid, "audio/use_lock.wav");
    say(event.peer, pPeer, std::format("You mounted your `9{}`` `2{} lb. {}``!", q, lbs, id_to_item(id).raw_name));
    pPeer->mysql_update_all();
    world->save();
}

/* ---------------- Lobster Trap ---------------- */

void lobster_trap_wrench(ENetEvent &event, ::world &world, const ::pos &pos)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (!world.can_edit(*pPeer, pos, false)) { play_sfx(event.peer, "audio/cant_place_tile.wav"); return; }
    auto trap = std::ranges::find(world.lobster_traps, pos, &::lobster_trap::pos);
    const bool loaded = trap != world.lobster_traps.end() && trap->count > 0;

    ::create_dialog dialog = ::create_dialog()
        .set_default_color("`o")
        .add_label_with_icon("big", "`wLobster Trap``", 3448)
        .add_spacer("small");
    if (loaded)
    {
        dialog.add_label_with_icon("small", std::format("`wThe trap is baited with {} {}.``", trap->count, id_to_item(trap->bait).raw_name), trap->bait)
              .add_textbox("`oPunch the trap now and then to check it. It may be days before an animal wanders in!``")
              .add_button("take_bait", "`wTake the bait out``");
    }
    else
    {
        dialog.add_textbox("`oLoad this trap with 5 of any type of bait, then check it by punching it now and then.``")
              .add_item_picker("bait", "`wLoad bait``", "Choose a bait (5 are needed)");
    }
    send(event, pos, dialog, "lobster_trap", "Close", "");
}

void lobster_trap_edit(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::world *world = world_of(pPeer);
    ::pos pos{};
    if (world == nullptr || !read_pos(hPipe, *world, type::LOBSTER_TRAP, pos) || !world->can_edit(*pPeer, pos, false)) return;
    auto found = std::ranges::find(world->lobster_traps, pos, &::lobster_trap::pos);
    if (found == world->lobster_traps.end()) found = world->lobster_traps.emplace(world->lobster_traps.end(), ::lobster_trap{.pos = pos});
    ::lobster_trap &trap = *found;

    if (const short id = static_cast<short>(atoi(hPipe["bait"].c_str())); id > 0)
    {
        if (trap.count > 0) return;
        if (!is_bait(id)) throw std::runtime_error("That's not bait!");
        if (pPeer->have_slot(::slot(id, 0)) < 5) throw std::runtime_error("You need 5 of that bait to load the trap!");
        modify_item_inventory(event, ::slot(id, -5));
        trap.bait = id, trap.count = 5, trap.checked = ticks();
        say(event.peer, pPeer, std::format("You loaded the trap with 5 `2{}``.", id_to_item(id).raw_name));
    }
    else if (hPipe["buttonClicked"] == "take_bait" && trap.count > 0)
    {
        if (!pPeer->can_hold(trap.bait, trap.count)) throw std::runtime_error("You don't have room in your backpack!");
        modify_item_inventory(event, ::slot(trap.bait, static_cast<short>(trap.count)));
        trap.count = 0, trap.bait = 0;
    }
    else return;
    pPeer->mysql_update_all();
    world->save();
}

bool lobster_trap_punch(ENetEvent &event, ::world &world, const ::pos &pos)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    auto trap = std::ranges::find(world.lobster_traps, pos, &::lobster_trap::pos);
    if (trap == world.lobster_traps.end() || trap->count <= 0) return false; // @note empty: the punch breaks it like any block

    const u_int hours = (ticks() - trap->checked) / 3600;
    if (hours == 0)
    {
        say(event.peer, pPeer, "Nothing in the trap yet. Check back later!");
        return true;
    }
    trap->checked += hours * 3600;
    for (u_int h = 0; h < std::min(hours, 72u); ++h) // @note every hour an animal may wander in, or out with the bait
    {
        const int roll = RandomRange(0, 100);
        if (roll < 4)
        {
            const short crab = pick(crabs);
            const int lbs = RandomRange(1, fish_max_size(crab) + 1);
            trap->count = 0, trap->bait = 0;
            if (!pPeer->can_hold(crab, lbs))
            {
                say(event.peer, pPeer, std::format("A `2{} lb. {}`` was in the trap, but your backpack was too full and it escaped!", lbs, id_to_item(crab).raw_name));
                world.save();
                return true;
            }
            modify_item_inventory(event, ::slot(crab, static_cast<short>(lbs)));
            play_in_world(world.name, pPeer->netid, "audio/teleport.wav");
            const std::string q = quality(crab, lbs);
            say(event.peer, pPeer, std::format("You found a {}`2{} lb. {}`` in the trap!", q.empty() ? "" : std::format("`9{}`` ", q), lbs, id_to_item(crab).raw_name));
            world.save();
            return true;
        }
        if (roll < 6)
        {
            trap->count = 0, trap->bait = 0;
            say(event.peer, pPeer, "Something wandered in and back out with your bait!");
            world.save();
            return true;
        }
    }
    say(event.peer, pPeer, "Nothing in the trap yet. Check back later!");
    world.save();
    return true;
}
