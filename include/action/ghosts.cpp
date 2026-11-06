#include "pch.hpp"

#include "onVariant/ConsoleMessage.hpp"
#include "onVariant/SetClothing.hpp"
#include "tools/create_dialog.hpp"
#include "tools/sound.hpp"
#include "tools/random.hpp"
#include "action/respawn.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>

#include "ghosts.hpp"

namespace
{
    /* @note NPC types and events, read from the game client's own name tables (NPC_GHOST, NPC_EVENT_ADD, ...) */
    constexpr u_char NPC_GHOST       = 1;
    constexpr u_char NPC_GHOST_JAR   = 2;
    constexpr u_char NPC_MIND_GHOST  = 12;

    constexpr u_char EV_DELETE = 1;
    constexpr u_char EV_ADD    = 2;
    constexpr u_char EV_MOVE   = 3;
    constexpr u_char EV_SUCKED = 4; // @note count1 the ghost, object type the jar: both go, with the jar's particle
    constexpr u_char EV_DIE    = 7;
    constexpr u_char EV_PUNCH  = 8; // @note sent by the game: the Neutron Power Glove hit this NPC

    constexpr int MAX_GHOSTS            = 25;    // @note "Each world is capped at 25 ghosts"
    constexpr long long SLIME_MS        = 6000;  // @note slowed / controls turned around for 6 seconds
    constexpr long long JAR_ARMS_MS     = 2000;  // @note "A jar takes 2 seconds to activate,
    constexpr long long JAR_LASTS_MS    = 5000;  //        and then only collects spirits for 5 seconds"
    constexpr long long HOLD_MS         = 1500;  // @note how long one Neutron Gun punch holds a ghost
    constexpr long long CROSS_MS        = 600;   // @note two beams this close in time count as crossed
    constexpr long long BUSY_SPAWN_MS   = 5ll * 60 * 1000; // @note while people are in a world, a ghost may wander in every 5 minutes
    constexpr long long EMPTY_HOURS     = 6;     // @note a world nobody opened gets a ghost for every 6 hours it stood empty
    constexpr float BEAM_LENGTH         = 7.0f * 32.0f;
    constexpr float BEAM_WIDTH          = 30.0f;
    constexpr float GLOVE_REACH         = 48.0f;
    constexpr float SUCK_SPEED          = 140.0f; // @note how fast a jar pulls a ghost in (pixels a second)

    long long now_ms()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    ::world *world_of(const ::peer &peer)
    {
        if (peer.netid == 0) return nullptr;
        auto it = std::ranges::find(worlds, peer.recent_worlds.back(), &::world::name);
        return (it != worlds.end()) ? &*it : nullptr;
    }

    bool wearing(const ::peer &peer, short id)
    {
        return std::ranges::any_of(peer.clothing, [id](float c) { return static_cast<short>(c) == id; });
    }

    /* @note the middle of the player, in pixels */
    std::pair<float, float> centre(const ::peer &peer) { return { peer.pos.x + 16.0f, peer.pos.y + 16.0f }; }

    float world_height_px(const ::world &world) { return static_cast<float>(world.blocks.size() / 100) * 32.0f; }

    /* @return where the ghost is right now: it floats in a straight line from (x, y) to (dx, dy) */
    std::pair<float, float> where(const ::ghost &g, long long now)
    {
        const float vx = g.dx - g.x, vy = g.dy - g.y;
        const float length = std::sqrt(vx * vx + vy * vy);
        if (length < 0.5f || g.since == 0) return { g.dx, g.dy };
        const float done = g.speed * static_cast<float>(now - g.since) / 1000.0f;
        if (done >= length) return { g.dx, g.dy };
        return { g.x + vx * done / length, g.y + vy * done / length };
    }

    ::blob npc(u_char event, u_char kind, u_char id, float x, float y, float dx, float dy, float speed)
    {
        return compress_state(::gamePacket{
            .type = static_cast<int>(0x22u | (u_int(kind) << 8) | (u_int(id) << 16) | (u_int(event) << 24)), // @note PACKET_NPC: type, NPC type, NPC id, event
            .pos = ::pos{ x, y },     // @note where it is
            .speed = ::pos{ dx, dy }, // @note where it goes
            .idk = speed              // @note pixels a second
        });
    }

    void to_world(const ::world &world, const ::blob &blob)
    {
        peers(world.name, PeerSearch::WORLD, [&blob](ENetPeer *peer) { send_data(*peer, blob); });
    }

    void send_ghost(const ::world &world, const ::ghost &g, u_char event, long long now, ENetPeer *only = nullptr)
    {
        const auto [x, y] = where(g, now);
        const ::blob blob = npc(event, g.kind, g.id, x, y, g.dx, g.dy, g.speed);
        if (only != nullptr) send_data(*only, blob);
        else to_world(world, blob);
    }

    void send_jar(const ::world &world, const ::ghost_jar &jar, u_char event, ENetPeer *only = nullptr)
    {
        const ::blob blob = npc(event, NPC_GHOST_JAR, jar.id, jar.x, jar.y, jar.x, jar.y, 0.0f);
        if (only != nullptr) send_data(*only, blob);
        else to_world(world, blob);
    }

    /* @return an NPC id nobody in this world uses, 0 if all 255 are taken */
    u_char free_id(const ::world &world)
    {
        for (int id = 1; id < 256; ++id)
        {
            if (std::ranges::any_of(world.ghosts, [id](const ::ghost &g) { return g.id == id; })) continue;
            if (std::ranges::any_of(world.ghost_jars, [id](const ::ghost_jar &j) { return j.id == id; })) continue;
            if (std::ranges::any_of(world.npc_removals, [id](const auto &r) { return r.second[0] == id || r.second[1] == id; })) continue;
            return static_cast<u_char>(id);
        }
        return 0;
    }

    float ghost_speed(u_char kind) { return (kind == NPC_MIND_GHOST) ? static_cast<float>(RandomRange(85, 115)) : static_cast<float>(RandomRange(35, 55)); }

    /* @brief the ghost turns towards (nx, ny) from wherever it is now */
    void set_course(::ghost &g, float nx, float ny, float speed, long long now)
    {
        const auto [x, y] = where(g, now);
        g.x = x, g.y = y;
        g.dx = nx, g.dy = ny;
        g.speed = speed;
        g.since = now;
    }

    void clamp_to_world(const ::world &world, float &x, float &y)
    {
        x = std::clamp(x, 32.0f, 99.0f * 32.0f - 16.0f);
        y = std::clamp(y, 32.0f, std::max(64.0f, world_height_px(world) - 7.0f * 32.0f)); // @note not down in the bedrock
    }

    bool charm_on(const ::world &world)
    {
        return std::ranges::any_of(world.blocks, [](const ::block &b) { return b.fg == GHOST_CHARM && (b.state[2] & S_TOGGLE); });
    }
    bool ecto_lock(const ::world &world)
    {
        return std::ranges::any_of(world.blocks, [](const ::block &b) { return b.fg == 8470/*Ecto-Lock*/; });
    }

    /* @return the new ghost, or nullptr when the world is full */
    ::ghost *spawn(::world &world, u_char kind, float x, float y, long long now, bool announce = true)
    {
        if (static_cast<int>(world.ghosts.size()) >= MAX_GHOSTS) return nullptr;
        const u_char id = free_id(world);
        if (id == 0) return nullptr;
        clamp_to_world(world, x, y);
        ::ghost &g = world.ghosts.emplace_back(::ghost{ .id = id, .kind = kind, .x = x, .y = y, .dx = x, .dy = y });
        g.speed = ghost_speed(kind);
        g.since = now;
        g.next = now + RandomRange(500, 2500);
        if (announce) send_ghost(world, g, EV_ADD, now);
        return &g;
    }

    ::ghost *spawn_random(::world &world, long long now)
    {
        const int rows = static_cast<int>(world.blocks.size() / 100);
        const float x = static_cast<float>(RandomRange(2, 97)) * 32.0f + 16.0f;
        const float y = static_cast<float>(RandomRange(2, std::max(3, rows - 10))) * 32.0f + 16.0f;
        return spawn(world, (RandomRange(1, 100) <= 20) ? NPC_MIND_GHOST : NPC_GHOST, x, y, now); // @note "Replaces some ghosts"
    }

    /* @brief the ghost is gone (banished, caught, cleared) */
    void remove_ghost(::world &world, u_char id, u_char event, long long now)
    {
        auto g = std::ranges::find(world.ghosts, id, &::ghost::id);
        if (g == world.ghosts.end()) return;
        send_ghost(world, *g, event, now);
        if (event != EV_DELETE) world.npc_removals.emplace_back(now + 1500, std::array<u_char, 2>{ id, 0 }); // @note let the game play it out, then make sure it's gone
        world.ghosts.erase(g);
    }

    ENetPeer *anyone_in(const ::world &world)
    {
        ENetPeer *found = nullptr;
        peers(world.name, PeerSearch::WORLD, [&found](ENetPeer *peer) { if (found == nullptr) found = peer; });
        return found;
    }

    /* @brief the "sound" comes from this player (OnPlayPositioned) */
    void sound_near(const ::world &world, float x, float y, std::string_view file)
    {
        int netid = 0;
        float best = 1e9f;
        peers(world.name, PeerSearch::WORLD, [&](ENetPeer *peer)
        {
            const ::peer *p = static_cast<::peer*>(peer->data);
            const auto [px, py] = centre(*p);
            const float d = std::hypot(px - x, py - y);
            if (d < best) best = d, netid = p->netid;
        });
        if (netid != 0) play_in_world(world.name, netid, file);
    }

    void drop_at(::world &world, short item, int count, float x, float y)
    {
        ENetPeer *peer = anyone_in(world);
        if (peer == nullptr) return;
        ENetEvent fake{ .peer = peer };
        add_drop(fake, ::slot(item, static_cast<short>(count)), ::pos{ x - 16.0f, y - 16.0f }, world);
    }

    float seg_distance(float px, float py, float ax, float ay, float bx, float by, float *along = nullptr)
    {
        const float vx = bx - ax, vy = by - ay;
        const float len2 = vx * vx + vy * vy;
        float t = (len2 > 0.0f) ? ((px - ax) * vx + (py - ay) * vy) / len2 : 0.0f;
        t = std::clamp(t, 0.0f, 1.0f);
        if (along != nullptr) *along = t * std::sqrt(len2);
        return std::hypot(px - (ax + vx * t), py - (ay + vy * t));
    }

    bool segments_cross(const float a[4], const float b[4])
    {
        auto side = [](float ax, float ay, float bx, float by, float cx, float cy) { return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax); };
        const float d1 = side(b[0], b[1], b[2], b[3], a[0], a[1]);
        const float d2 = side(b[0], b[1], b[2], b[3], a[2], a[3]);
        const float d3 = side(a[0], a[1], a[2], a[3], b[0], b[1]);
        const float d4 = side(a[0], a[1], a[2], a[3], b[2], b[3]);
        return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0));
    }

    void bubble(ENetEvent &event, const std::string &text)
    {
        const ::peer *pPeer = static_cast<::peer*>(event.peer->data);
        send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, text, 0u, 1u });
    }

    /* @brief Neutron Gun: a beam from the player through the punched tile. ghosts on it are held and pulled in */
    void fire_beam(ENetEvent &event, ::world &world, const ::pos &tile)
    {
        ::peer *pPeer = static_cast<::peer*>(event.peer->data);
        const long long now = now_ms();
        const auto [cx, cy] = centre(*pPeer);
        float tx = tile.x * 32.0f + 16.0f - cx, ty = tile.y * 32.0f + 16.0f - cy;
        float length = std::hypot(tx, ty);
        if (length < 1.0f) tx = pPeer->facing_left ? -1.0f : 1.0f, ty = 0.0f, length = 1.0f;
        tx /= length, ty /= length;
        const float ex = cx + tx * BEAM_LENGTH, ey = cy + ty * BEAM_LENGTH;

        for (::ghost &g : world.ghosts)
        {
            if (g.into_jar != 0) continue; // @note already going into a jar
            const auto [gx, gy] = where(g, now);
            float along{};
            if (seg_distance(gx, gy, cx, cy, ex, ey, &along) > BEAM_WIDTH) continue;
            const float keep = std::max(40.0f, along - 72.0f); // @note pulled along the beam towards the shooter
            float nx = cx + tx * keep, ny = cy + ty * keep;
            clamp_to_world(world, nx, ny);
            set_course(g, nx, ny, 110.0f, now);
            g.held_until = now + HOLD_MS;
            g.next = g.held_until;
            send_ghost(world, g, EV_MOVE, now);
        }

        /* @note "When two or more beams cross, all of the players who shot the beam will die." */
        const float mine[4] = { cx, cy, ex, ey };
        std::vector<ENetPeer*> crossed{};
        peers(world.name, PeerSearch::WORLD, [&](ENetPeer *peer)
        {
            ::peer *other = static_cast<::peer*>(peer->data);
            if (peer == event.peer || other->beam_ms == 0 || now - other->beam_ms > CROSS_MS) return;
            if (segments_cross(mine, other->beam)) crossed.push_back(peer);
        });
        std::copy(std::begin(mine), std::end(mine), pPeer->beam);
        pPeer->beam_ms = now;
        if (!crossed.empty())
        {
            crossed.push_back(event.peer);
            for (ENetPeer *peer : crossed)
            {
                ::peer *p = static_cast<::peer*>(peer->data);
                p->beam_ms = 0;
                on::ConsoleMessage(peer, "`4Don't cross the beams!`` It would be bad.");
                ENetEvent fake{ .peer = peer };
                action::respawn(fake, "");
            }
        }
    }

    /* @brief Neutron Power Glove: punching banishes ghosts near the fist */
    int glove_punch(ENetEvent &event, ::world &world, float px, float py)
    {
        const long long now = now_ms();
        std::vector<u_char> hit{};
        for (const ::ghost &g : world.ghosts)
        {
            if (g.into_jar != 0) continue;
            const auto [gx, gy] = where(g, now);
            if (std::hypot(gx - px, gy - py) <= GLOVE_REACH) hit.push_back(g.id);
        }
        for (const u_char id : hit) remove_ghost(world, id, EV_DIE, now);
        if (!hit.empty())
        {
            const ::peer *pPeer = static_cast<::peer*>(event.peer->data);
            play_in_world(world.name, pPeer->netid, "audio/boo_proton_glove.wav");
            on::ConsoleMessage(event.peer, (hit.size() == 1) ? "`2You banished a ghost!``" : std::format("`2You banished {} ghosts!``", hit.size()));
        }
        return static_cast<int>(hit.size());
    }

    /* @note what an exploding Spirit Storage Unit gives, by how full it was (percent). source: the SSU page on the Growtopia wiki */
    struct ssu_prize { int from, to; std::vector<short> items; };
    const std::vector<ssu_prize> &ssu_prizes()
    {
        static const std::vector<ssu_prize> prizes = {
            {105, 115, {3734}},  // Warning Background
            {120, 130, {3732}},  // Warning Block
            {135, 140, {3748}},  // Bone Spikes
            {145, 170, {3712, 3706, 3708, 3718, 11136}}, // B.O.O. Helmet, Shirt, Pants, Rubber Boots, Mystery Portal
            {175, 195, {3728}},  // Ectoplasm Sample
            {200, 205, {10056}}, // Ghost Block
            {205, 220, {3730}},  // Crystallized Ghost
            {225, 225, {3788}},  // Riding Broomstick
            {230, 240, {3750}},  // Ghost Charm
            {245, 245, {3738}},  // Haunted Pants
            {250, 255, {6060}},  // Dark Spirit Board
            {260, 265, {3738}},  // Haunted Pants
            {265, 9999, {6840, 11764}}, // Dreamcatcher Staff, Edison, The Lightbulb
            {270, 9999, {3736}}, // Demon Control Cap
        };
        return prizes;
    }

    short roll_ssu_prize(int percent)
    {
        std::vector<short> pool{};
        for (const ssu_prize &p : ssu_prizes())
            if (percent >= p.from && percent <= p.to) pool.insert(pool.end(), p.items.begin(), p.items.end());
        if (pool.empty()) // @note between two bands: the band just below
            for (const ssu_prize &p : ssu_prizes())
                if (percent > p.to) pool = p.items;
        if (pool.empty()) return 3734;
        return pool[RandomRange(0, static_cast<int>(pool.size()) - 1)];
    }

    /* @return the chance (0..1) that the ghost going in now blows the unit up */
    float explode_chance(int percent_after, bool command)
    {
        if (percent_after <= 100) return 0.0f;
        float chance = 0.04f + static_cast<float>(percent_after - 105) / 5.0f * 0.025f; // @note "that chance increases with each additional ghost"
        if (command && percent_after < 200) chance *= 0.5f; // @note Ghostking's Command: "decrease the odds ... at lower, less-lucrative levels"
        return std::clamp(chance, 0.0f, 0.9f);
    }

    void ssu_wrench(ENetEvent &event, ::world &world, const ::pos &pos)
    {
        auto ssu = std::ranges::find(world.spirit_units, pos, &::spirit_unit::pos);
        const int ghosts = (ssu != world.spirit_units.end()) ? ssu->ghosts : 0;
        const int percent = ghosts * 5;
        ::create_dialog dialog = ::create_dialog()
            .set_default_color("`o")
            .add_label_with_icon("big", "`wSpirit Storage Unit``", SPIRIT_STORAGE_UNIT)
            .add_textbox(std::format("`2Spirit Storage``: {} ghost{} stored, `w{}%`` full.", ghosts, (ghosts == 1) ? "" : "s", percent))
            .add_spacer("small")
            .add_smalltext("Drop Ghosts-In-A-Jar in front of it, then send Steam into it to store them. Every ghost fills it 5%.")
            .add_smalltext(percent >= 100 ?
                "`4It's overloaded!`` Every ghost you add now could make it explode. The fuller it is when it blows, the better the prize." :
                "Past 100% it starts to overload, and every new ghost might make it explode. The fuller it is when it blows, the better the prize.");
        send_varlist(event.peer, { "OnDialogRequest", dialog.end_dialog("ssu_info", "", "OK") });
    }
}

float ghost_speed_factor(const ::peer &peer)
{
    return (peer.slimed_until > now_ms()) ? 0.5f : 1.0f; // @note slimed: half speed. a Mind Ghost doesn't slow you, it swaps your controls (ghost_flags2)
}

int ghost_flags2(const ::peer &peer)
{
    return (peer.mind_until > now_ms()) ? CHAR_FLAG2_MIND_CONTROL : 0;
}

u_int ghost_skin(const ::peer &peer, u_int skin)
{
    const long long now = now_ms();
    if (peer.mind_until > now) return 0xD86CB4FFu;  // @note purple, the Mind Ghost's colour (0xBBGGRRAA like every skin colour)
    if (peer.slimed_until > now) return 0x4AD86EFFu; // @note slime green
    return skin;
}

std::string ghost_effects(const ::peer &peer)
{
    const long long now = now_ms();
    std::string lines{};
    if (peer.slimed_until > now)
        lines += std::format("add_label_with_icon|small|`wSlimed`` `o({} secs left)``|left|{}|\n", (peer.slimed_until - now + 999) / 1000, ECTOPLASM);
    if (peer.mind_until > now)
        lines += std::format("add_label_with_icon|small|`wMind Control`` `o({} secs left)``|left|{}|\n", (peer.mind_until - now + 999) / 1000, MIND_GHOST_IN_A_JAR);
    return lines;
}

bool ghost_tile_change(ENetEvent &event, ::world &world, const ::gamePacket &gamePacket)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const ::pos tile{ gamePacket.punch.x_int(), gamePacket.punch.y_int() };
    ::block &block = world.blocks[cord(tile.x_int(), tile.y_int())];
    const long long now = now_ms();
    const float tx = tile.x * 32.0f + 16.0f, ty = tile.y * 32.0f + 16.0f;

    if (gamePacket.id == 32 && block.fg == SPIRIT_STORAGE_UNIT)
    {
        ssu_wrench(event, world, tile);
        return true;
    }
    if (gamePacket.id == 18)
    {
        const bool pack = pPeer->clothing[clothing::BACK] == NEUTRON_PACK;
        if (pPeer->clothing[clothing::HAND] == NEUTRON_GUN)
        {
            if (!pack)
            {
                bubble(event, "You'll need to wear a `2Neutron Pack`` to power the Neutron Gun!");
                return true; // @note the gun "has no impact on our world"
            }
            fire_beam(event, world, tile);
            return true;
        }
        if (pPeer->clothing[clothing::HAND] == NEUTRON_POWER_GLOVE && pack) glove_punch(event, world, tx, ty); // @note the punch still lands
        return false;
    }
    if (gamePacket.id == GHOST_JAR || gamePacket.id == GHOST_TRAP)
    {
        if (block.fg != 0 && id_to_item(block.fg).collision == collision::FULL) throw std::runtime_error("There's no room to put that there!");
        if (std::ranges::any_of(world.ghost_jars, [&](const ::ghost_jar &j) { return !j.done && j.x == tx && j.y == ty; }))
            throw std::runtime_error("There's already a jar there!");
        if (std::ranges::count_if(world.ghost_jars, [](const ::ghost_jar &j) { return !j.done; }) >= 10) throw std::runtime_error("Too many jars are out already!");
        const u_char id = free_id(world);
        if (id == 0) throw std::runtime_error("Too many ghosts here to set that down!");
        modify_item_inventory(event, ::slot(gamePacket.id, -1));
        const ::ghost_jar &jar = world.ghost_jars.emplace_back(::ghost_jar{ .id = id, .item = gamePacket.id, .x = tx, .y = ty, .placed = now });
        send_jar(world, jar, EV_ADD);
        play_in_world(world.name, pPeer->netid, "audio/boo_ghost_trap.wav");
        return true;
    }
    if (gamePacket.id == GHOST_IN_A_JAR || gamePacket.id == MIND_GHOST_IN_A_JAR)
    {
        if (spawn(world, (gamePacket.id == MIND_GHOST_IN_A_JAR) ? NPC_MIND_GHOST : NPC_GHOST, tx, ty, now) == nullptr)
            throw std::runtime_error("This world is too haunted already!");
        modify_item_inventory(event, ::slot(gamePacket.id, -1));
        modify_item_inventory(event, ::slot(GHOST_JAR, 1)); // @note the ghost is out, the empty jar is left
        play_in_world(world.name, pPeer->netid, "audio/boo_evil_laugh.wav");
        return true;
    }
    if (gamePacket.id == GHOST_BE_GONE)
    {
        if (world.ghosts.empty()) throw std::runtime_error("There are no ghosts here!");
        modify_item_inventory(event, ::slot(GHOST_BE_GONE, -1));
        const int gone = ghosts_clear(world);
        play_in_world(world.name, pPeer->netid, "audio/boo_ghost_be_gone.wav");
        peers(world.name, PeerSearch::WORLD, [&](ENetPeer *peer) {
            on::ConsoleMessage(peer, std::format("`2{} used Ghost-Be-Gone!`` {} ghost{} vanished.", pPeer->growid, gone, (gone == 1) ? "" : "s"));
        });
        return true;
    }
    return false;
}

int ghosts_clear(::world &world)
{
    const long long now = now_ms();
    const int count = static_cast<int>(world.ghosts.size());
    while (!world.ghosts.empty()) remove_ghost(world, world.ghosts.back().id, EV_DIE, now);
    world.save();
    return count;
}

void ghost_npc_packet(ENetEvent &event, ::gamePacket gamePacket)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::world *world = world_of(*pPeer);
    if (world == nullptr) return;
    const u_char ev = static_cast<u_char>((static_cast<u_int>(gamePacket.type) >> 24) & 0xff);
    const u_char id = static_cast<u_char>((static_cast<u_int>(gamePacket.type) >> 16) & 0xff);
    if (ev != EV_PUNCH) return;
    if (pPeer->clothing[clothing::HAND] != NEUTRON_POWER_GLOVE || pPeer->clothing[clothing::BACK] != NEUTRON_PACK) return;

    const long long now = now_ms();
    auto g = std::ranges::find(world->ghosts, id, &::ghost::id);
    if (g == world->ghosts.end()) return;
    const auto [gx, gy] = where(*g, now);
    const auto [cx, cy] = centre(*pPeer);
    if (std::hypot(gx - cx, gy - cy) > 5.0f * 32.0f) return; // @note out of reach (the game says it hit, but the ghost is far off here)
    glove_punch(event, *world, gx, gy);
}

namespace
{
    /* @brief a ghost touching this player slimes them (or swaps their controls). @return true if it did */
    bool ghost_touch(ENetPeer *peer, ::world &world, long long now)
    {
        ::peer *pPeer = static_cast<::peer*>(peer->data);
        if (pPeer == nullptr || pPeer->netid == 0 || world.ghosts.empty()) return false;
        if (pPeer->state & S_GHOST) return false; // @note /ghost: walking through everything

        /* @note the player's body is about 20x30 in the middle of their 32x32 tile, a ghost about 28x28 around its point */
        const float px = pPeer->pos.x + 16.0f, py = pPeer->pos.y + 17.0f;
        const bool king = wearing(*pPeer, 8496/*Ghostking's Glory*/);
        for (const ::ghost &g : world.ghosts)
        {
            if (g.id == 0 || g.into_jar != 0) continue;
            const auto [gx, gy] = where(g, now);
            if (std::abs(gx - px) > 24.0f || std::abs(gy - py) > 29.0f) continue;

            if (g.kind == NPC_MIND_GHOST)
            {
                if (king || wearing(*pPeer, 6092/*Foil Hat*/) || wearing(*pPeer, 6374/*Alien Mind Protector*/)) continue;
                const bool fresh = pPeer->mind_until <= now;
                pPeer->mind_until = now + SLIME_MS; // @note touching it again keeps it going
                if (!fresh) continue;
                on::ConsoleMessage(peer, "`oA Mind Ghost got into your head! Left is right and right is left! (`$Mind Control`o mod added, `$6 secs`o left)``");
                send_varlist(peer, { "OnTalkBubble", pPeer->netid, "`5Whoa... my head!``", 0u, 1u });
            }
            else
            {
                if (king || wearing(*pPeer, RUBBER_BOOTS)) continue;
                const bool fresh = pPeer->slimed_until <= now;
                pPeer->slimed_until = now + SLIME_MS;
                if (!fresh) continue;
                on::ConsoleMessage(peer, "`oYou've been slimed! You feel sluggish. (`$Slimed`o mod added, `$6 secs`o left)``");
                send_varlist(peer, { "OnTalkBubble", pPeer->netid, "`2I got slimed!``", 0u, 1u });
            }
            play_in_world(world.name, pPeer->netid, "audio/boo_evil_laugh.wav");
            on::SetClothing(peer, false, true); // @note everyone sees the new colour
            return true;
        }
        return false;
    }
}

void ghost_moved(ENetEvent &event)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::world *world = world_of(*pPeer);
    if (world == nullptr) return;
    ghost_touch(event.peer, *world, now_ms());
}

void ghost_entered(ENetEvent &event, ::world &world)
{
    const long long now = now_ms();
    const long long unix_now = std::time(nullptr);

    /* @note ghosts kept from last time get NPC ids now */
    for (::ghost &g : world.ghosts)
    {
        if (g.id != 0) continue;
        g.id = free_id(world);
        g.speed = ghost_speed(g.kind);
        g.since = now;
        g.next = now + RandomRange(500, 4000);
    }
    std::erase_if(world.ghosts, [](const ::ghost &g) { return g.id == 0; });

    /* @note the first one in: ghosts moved in while nobody was here ("haunted worlds" are the ones nobody visited for a long time) */
    if (world.visitors <= 1)
    {
        if (world.ghosts_checked > 0 && !charm_on(world))
        {
            const long long hours = (unix_now - world.ghosts_checked) / 3600;
            int arrivals = static_cast<int>(std::min<long long>(hours / EMPTY_HOURS, MAX_GHOSTS));
            if (ecto_lock(world)) arrivals *= 2;
            for (int i = 0; i < arrivals; ++i) if (spawn_random(world, now) == nullptr) break;
        }
        world.ghosts_checked = unix_now;
    }

    for (const ::ghost &g : world.ghosts) send_ghost(world, g, EV_ADD, now, event.peer);
    for (const ::ghost_jar &jar : world.ghost_jars) if (!jar.done) send_jar(world, jar, EV_ADD, event.peer);
}

int ssu_steamed(::world &world, const ::pos &tile)
{
    if (!valid_tile(world, tile)) return 0;
    ::block &block = world.blocks[cord(tile.x_int(), tile.y_int())];
    if (block.fg != SPIRIT_STORAGE_UNIT) return 0;
    ENetPeer *someone = anyone_in(world);
    if (someone == nullptr) return 0;
    ENetEvent event{ .peer = someone };

    /* @note "Drop Ghosts-In-A-Jar in front of it, then provide it with Steam power": jars lying on its tile or right next to it */
    const auto near_unit = [&tile](const ::object &o) {
        for (const ::pos at : { o.pos.by_32(true), ::pos{ o.pos.x + 8.0f, o.pos.y + 8.0f }.by_32(true) })
            if (at.y == tile.y && std::abs(at.x - tile.x) <= 1.0f) return true;
        return false;
    };
    int jars = 0;
    std::vector<u_int> used{};
    for (const ::object &o : world.objects)
        if ((o.id == GHOST_IN_A_JAR || o.id == MIND_GHOST_IN_A_JAR) && near_unit(o)) jars += o.count, used.push_back(o.uid);
    if (jars == 0) return 0;
    for (const u_int uid : used)
    {
        std::erase_if(world.objects, [uid](const ::object &o) { return o.uid == uid; });
        remove_object(event, static_cast<int>(uid));
    }

    auto ssu = std::ranges::find(world.spirit_units, tile, &::spirit_unit::pos);
    if (ssu == world.spirit_units.end()) ssu = world.spirit_units.insert(world.spirit_units.end(), ::spirit_unit{ .pos = tile });

    /* @note Ghostking's Command: the world owner wearing it here halves the risk below 200% */
    bool command = false;
    peers(world.name, PeerSearch::WORLD, [&](ENetPeer *p) {
        const ::peer *pp = static_cast<::peer*>(p->data);
        if (wearing(*pp, 9098) && pp->owns(world.owner)) command = true;
    });
    const float x = tile.x * 32.0f + 16.0f, y = tile.y * 32.0f + 16.0f;
    int taken = 0;
    while (taken < jars)
    {
        ++taken;
        ++ssu->ghosts;
        const int percent = ssu->ghosts * 5;
        const float chance = explode_chance(percent, command);
        if (chance > 0.0f && static_cast<float>(RandomRange(0, 9999)) < chance * 10000.0f)
        {
            /* @note boom: the unit is wrecked and gives a prize for how full it was. jars it hadn't taken yet fall back out */
            const short prize = roll_ssu_prize(percent);
            world.spirit_units.erase(ssu);
            block.fg = DESTROYED_SSU;
            block.hits[0] = 0;
            world_tile_update(world, tile);
            sound_near(world, x, y, "audio/bigboom.wav");
            drop_at(world, prize, 1, x, y);
            if (jars - taken > 0) drop_at(world, GHOST_IN_A_JAR, jars - taken, x + 32.0f, y);
            peers(world.name, PeerSearch::WORLD, [&](ENetPeer *peer) {
                on::ConsoleMessage(peer, std::format("`4The Spirit Storage Unit exploded at {}%!`` Out of the wreck came a `2{}``!", percent, id_to_item(prize).raw_name));
            });
            world.save();
            return taken;
        }
    }
    const int percent = ssu->ghosts * 5;
    world_tile_update(world, tile);
    sound_near(world, x, y, (percent > 100) ? "audio/boo_pke_warning_light.wav" : "audio/boo_ghost_trap.wav");
    peers(world.name, PeerSearch::WORLD, [&](ENetPeer *peer) {
        on::ConsoleMessage(peer, std::format("`2The Spirit Storage Unit stored {} ghost{}!`` It's `w{}%`` full{}", taken, (taken == 1) ? "" : "s", percent, (percent > 100) ? " - `4it's overloading!``" : "."));
    });
    world.save();
    return taken;
}

void ghost_tile_cleared(::world &world, const ::pos &pos)
{
    std::erase_if(world.spirit_units, [&](const ::spirit_unit &s) { return s.pos == pos; });
}

void ghost_tick()
{
    static long long last{};
    const long long now = now_ms();
    if (now - last < 100) return;
    last = now;

    /* @note slime wears off */
    peers("", PeerSearch::EVERYONE, [now](ENetPeer *p)
    {
        ::peer *pp = static_cast<::peer*>(p->data);
        if (pp == nullptr) return;
        bool changed = false;
        if (pp->slimed_until != 0 && now >= pp->slimed_until)
        {
            pp->slimed_until = 0, changed = true;
            on::ConsoleMessage(p, "`oThe slime dries up. (`$Slimed`o mod removed)``");
        }
        if (pp->mind_until != 0 && now >= pp->mind_until)
        {
            pp->mind_until = 0, changed = true;
            on::ConsoleMessage(p, "`oYour mind is your own again. (`$Mind Control`o mod removed)``");
        }
        if (changed && pp->netid != 0) on::SetClothing(p, false, true);
    });

    static long long last_spawn_roll{};
    const bool spawn_roll = now - last_spawn_roll >= BUSY_SPAWN_MS;
    if (spawn_roll) last_spawn_roll = now;

    for (::world &world : worlds)
    {
        if (world.visitors == 0) continue;

        /* @note NPCs to take away now */
        for (auto it = world.npc_removals.begin(); it != world.npc_removals.end();)
        {
            if (now < it->first) { ++it; continue; }
            for (const u_char id : it->second) if (id != 0) to_world(world, npc(EV_DELETE, 0, id, 0, 0, 0, 0, 0));
            it = world.npc_removals.erase(it);
        }

        /* @note Ghost Jars and Ghost Traps */
        bool caught_any = false;
        for (auto g = world.ghosts.begin(); g != world.ghosts.end();)
        {
            if (g->into_jar == 0 || now < g->in_jar_at) { ++g; continue; }
            auto jar = std::ranges::find(world.ghost_jars, g->into_jar, &::ghost_jar::id);
            if (jar == world.ghost_jars.end()) { g->into_jar = 0; ++g; continue; }
            /* @note in it goes: the game takes both away with the jar's puff (NPC event SUCKED: count1 the ghost, object type the jar) */
            to_world(world, npc(EV_SUCKED, jar->id, g->id, jar->x, jar->y, jar->x, jar->y, 0.0f));
            world.npc_removals.emplace_back(now + 800, std::array<u_char, 2>{ g->id, jar->id }); // @note in case the game kept either
            sound_near(world, jar->x, jar->y, "audio/boo_ghost_trap.wav");
            drop_at(world, (g->kind == NPC_MIND_GHOST) ? MIND_GHOST_IN_A_JAR : GHOST_IN_A_JAR, 1, jar->x, jar->y);
            jar->done = true;
            caught_any = true;
            g = world.ghosts.erase(g);
        }
        for (::ghost_jar &jar : world.ghost_jars)
        {
            if (jar.done || jar.pulling) continue;
            const long long age = now - jar.placed;
            if (age >= JAR_ARMS_MS && age <= JAR_ARMS_MS + JAR_LASTS_MS)
            {
                const bool trap = jar.item == GHOST_TRAP; // @note "an enhanced field that is larger than a ghost jar"
                const float half = trap ? 48.0f : 26.0f, up = trap ? 128.0f : 88.0f;
                auto g = std::ranges::find_if(world.ghosts, [&](const ::ghost &g) {
                    if (g.into_jar != 0 || g.id == 0) return false;
                    const auto [gx, gy] = where(g, now);
                    return std::abs(gx - jar.x) <= half && gy <= jar.y + 16.0f && gy >= jar.y - up; // @note a ghost above it
                });
                if (g != world.ghosts.end())
                {
                    /* @note the jar pulls it in: it flies into the jar fast, then goes in (ghost_tick finishes it) */
                    const auto [gx, gy] = where(*g, now);
                    const float length = std::hypot(jar.x - gx, jar.y - gy);
                    set_course(*g, jar.x, jar.y - 4.0f, SUCK_SPEED, now);
                    g->into_jar = jar.id;
                    g->in_jar_at = now + std::max<long long>(250, static_cast<long long>(length / SUCK_SPEED * 1000.0f));
                    g->held_until = 0;
                    jar.pulling = true;
                    send_ghost(world, *g, EV_MOVE, now);
                    sound_near(world, jar.x, jar.y, "audio/boo_proton_glove.wav");
                    continue;
                }
            }
            if (age > JAR_ARMS_MS + JAR_LASTS_MS)
            {
                jar.done = true;
                send_jar(world, jar, EV_DELETE);
            }
        }
        std::erase_if(world.ghost_jars, [](const ::ghost_jar &j) { return j.done; });
        if (caught_any) world.save();

        /* @note ghosts drift into people standing still too, not only people walking into them */
        if (!world.ghosts.empty()) peers(world.name, PeerSearch::WORLD, [&world, now](ENetPeer *p) { ghost_touch(p, world, now); });

        /* @note ghosts float about. Ghostking's Glory: "All ghosts flock to the holder" */
        const ::peer *king = nullptr;
        peers(world.name, PeerSearch::WORLD, [&king](ENetPeer *p) {
            const ::peer *pp = static_cast<::peer*>(p->data);
            if (king == nullptr && wearing(*pp, 8496)) king = pp;
        });
        for (::ghost &g : world.ghosts)
        {
            if (g.id == 0 || g.into_jar != 0 || g.held_until > now || now < g.next) continue;
            float nx{}, ny{};
            float speed = ghost_speed(g.kind);
            if (king != nullptr)
            {
                const auto [kx, ky] = centre(*king);
                nx = kx + static_cast<float>(RandomRange(-40, 40)), ny = ky - static_cast<float>(RandomRange(16, 56));
            }
            else
            {
                const auto [gx, gy] = where(g, now);
                nx = gx + static_cast<float>(RandomRange(-6 * 32, 6 * 32));
                ny = gy + static_cast<float>(RandomRange(-4 * 32, 4 * 32));
                if (g.held_until != 0) speed *= 1.6f, g.held_until = 0; // @note let go: it bolts
            }
            clamp_to_world(world, nx, ny);
            set_course(g, nx, ny, speed, now);
            const float length = std::hypot(g.dx - g.x, g.dy - g.y);
            g.next = now + static_cast<long long>(length / std::max(1.0f, speed) * 1000.0f) + RandomRange(300, 2000);
            if (king != nullptr) g.next = std::min(g.next, now + 1200);
            send_ghost(world, g, EV_MOVE, now);
        }

        /* @note busy worlds: now and then a ghost wanders in, unless a Ghost Charm scares them off */
        if (spawn_roll && static_cast<int>(world.ghosts.size()) < MAX_GHOSTS && !charm_on(world))
        {
            const int chance = ecto_lock(world) ? 50 : 25;
            if (RandomRange(1, 100) <= chance && spawn_random(world, now) != nullptr) world.save();
        }
    }
}

void spawnghost_command(ENetEvent &event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::world *world = world_of(*pPeer);
    if (world == nullptr) return on::ConsoleMessage(event.peer, "Enter a world first.");

    bool mind = false;
    int count = 1;
    for (const std::string &word : readch(std::string(text), ' '))
    {
        if (word == "mind") mind = true;
        else if (!word.empty() && std::isdigit(static_cast<unsigned char>(word[0]))) count = std::clamp(atoi(word.c_str()), 1, MAX_GHOSTS);
    }
    const long long now = now_ms();
    const auto [cx, cy] = centre(*pPeer);
    int made = 0;
    for (int i = 0; i < count; ++i)
    {
        const float x = cx + static_cast<float>(RandomRange(-3 * 32, 3 * 32));
        const float y = cy - static_cast<float>(RandomRange(2 * 32, 4 * 32));
        if (spawn(*world, mind ? NPC_MIND_GHOST : NPC_GHOST, x, y, now) == nullptr) break;
        ++made;
    }
    world->save();
    on::ConsoleMessage(event.peer, std::format("`2{} {}ghost{} appeared!`` ({} in this world now)", made, mind ? "Mind " : "", (made == 1) ? "" : "s", world->ghosts.size()));
}
