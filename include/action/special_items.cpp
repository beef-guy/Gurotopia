#include "pch.hpp"

#include "onVariant/ConsoleMessage.hpp"
#include "onVariant/SetClothing.hpp"
#include "tools/create_dialog.hpp"
#include "tools/sound.hpp"
#include "tools/auth.hpp"
#include "action/quit_to_exit.hpp"
#include "action/join_request.hpp"

#include "special_items.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_map>

namespace
{
    constexpr short STAGING_PLATFORM  = 1616;
    constexpr short GAME_BLOCK_ID        = 1618;
    constexpr short BUSTED_GAME_BLOCK = 1620;
    constexpr short GAME_FLAG         = 1622;
    constexpr short MISSING_GAME_FLAG = 1624;
    constexpr short GAME_GRAVE        = 1656;
    constexpr short GAME_GOAL         = 1658;
    constexpr short CHALLENGE_START   = 3806;
    constexpr short CHALLENGE_END     = 3808;

    constexpr long long FREEZE_MS     = 10'000;  // @note "freeze them solid for 10 seconds"
    constexpr long long CURSE_SECONDS = 10 * 60; // @note "HIGHWAY TO HELL!! (For 10 minutes)"
    constexpr long long BAN_SECONDS   = 10 * 60; // @note "ban them for 10 minutes"
    constexpr int GAME_BLOCK_HITS     = 4;       // @note my pick: punches to smash an enemy Game Block during a game (20 normally)
    constexpr int VAULT_STACKS        = 20;      // @note my pick: how many stacks a Safe Vault holds
    constexpr long long VAULT_OPEN_MS = 5 * 60'000; // @note an unlocked vault stays open this long for the player

    const char *TEAM_NAMES[] = { "`4Red Rabbits``", "`1Blue Bombers``", "`6Yellow Yaks``", "`#Purple Penguins``", "`bNo team``" };

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

    ::peer *peer_of(ENetPeer *peer) { return static_cast<::peer*>(peer->data); }

    int tile_index(const ::pos &pos) { return cord(pos.x_int(), pos.y_int()); }

    /* @return the tile the middle of the player is in */
    ::pos center_tile(const ::peer &peer)
    {
        return ::pos{ std::floor((peer.pos.x + 16.0f) / 32.0f), std::floor((peer.pos.y + 16.0f) / 32.0f) };
    }

    /* @return true if the tile from tilex|tiley holds a block of this type */
    bool read_tile(const ::hPipe &hPipe, const ::world &world, u_char block_type, ::pos &pos)
    {
        pos = ::pos{atoi(hPipe["tilex"].c_str()), atoi(hPipe["tiley"].c_str())};
        if (!valid_tile(world, pos)) return false;
        return id_to_item(world.blocks[tile_index(pos)].fg).type == block_type;
    }

    bool is_editor(::world &world, const ::peer &peer, const ::pos &pos)
    {
        return peer.role >= MODERATOR || world.can_edit(peer, pos, false);
    }

    void dialog(ENetPeer *peer, const std::string &text) { send_varlist(peer, { "OnDialogRequest", text }); }

    void world_message(const std::string &world, const std::string &text)
    {
        peers(world, PeerSearch::WORLD, [&text](ENetPeer *p) { on::ConsoleMessage(p, text); });
    }

    void send_to_world(::world &world, ::gamePacket packet, const ::blob &extra = {})
    {
        ::blob blob = compress_state(packet);
        if (extra.size() > 0) blob.push_back(extra);
        peers(world.name, PeerSearch::WORLD, [&blob](ENetPeer *p) { send_data(*p, blob); });
    }

    /* @brief a call on one player, sent to everyone in their world */
    void world_varlist(const std::string &world, VariantList list, int netid)
    {
        peers(world, PeerSearch::WORLD, [&](ENetPeer *p) { send_varlist(p, list, netid); });
    }

    ENetPeer *find_peer(int uid)
    {
        ENetPeer *found = nullptr;
        peers("", PeerSearch::EVERYONE, [&](ENetPeer *p)
        {
            const ::peer *pp = peer_of(p);
            if (pp != nullptr && pp->user_id == uid) found = p;
        });
        return found;
    }

    /* ---------------- Game Generator ---------------- */

    u_char team_at(const ::world &world, const ::pos &pos)
    {
        auto t = std::ranges::find(world.game_tiles, pos, &::game_tile::pos);
        return (t != world.game_tiles.end()) ? t->team : 4;
    }

    int member_team(const ::world &world, int uid)
    {
        auto m = std::ranges::find(world.battle.members, uid, &std::pair<int, u_char>::first);
        return (m != world.battle.members.end()) ? m->second : -1;
    }

    /* @brief BATTLE_EVENT (0x1c): tileX = sub-command, tileY = value, itemID = team */
    void battle_event(::world &world, int sub, int netid, int value, int item = 0)
    {
        send_to_world(world, ::gamePacket{ .type = 0x1c, .netid = netid, .id = item, .punch = ::pos{sub, value} });
    }

    /* @return the team's Game Grave, or the main door */
    ::pos grave_of(const ::world &world, u_char team)
    {
        for (const ::game_tile &t : world.game_tiles)
            if (t.team == team && valid_tile(world, t.pos) && world.blocks[tile_index(t.pos)].fg == GAME_GRAVE) return t.pos;
        return ::pos{-1, -1};
    }

    void put_back(::world &world, const ::pos &pos)
    {
        auto it = std::ranges::find(world.battle.changed, pos, &std::pair<::pos, short>::first);
        if (it == world.battle.changed.end()) return;
        world.blocks[tile_index(pos)].fg = it->second;
        world.blocks[tile_index(pos)].hits[0] = 0;
        world.battle.changed.erase(it);
        world_tile_update(world, pos);
    }

    /* @brief the player stops carrying a flag: it goes back where it was */
    void drop_flag(::world &world, int uid, int netid, bool captured)
    {
        auto c = std::ranges::find(world.battle.carried, uid, &::battle::carry::uid);
        if (c == world.battle.carried.end()) return;
        const ::battle::carry carry = *c;
        world.battle.carried.erase(c);
        battle_event(world, 5, netid, 0xff, captured ? 1 : 0); // @note "<name> captured/dropped the <team> Flag!"
        put_back(world, carry.flag);
        battle_event(world, 5, -1, carry.team); // @note "The <team> Flag was returned!"
    }

    void send_respawn_tile(ENetPeer *p, const ::world &world, const ::pos &tile)
    {
        send_varlist(p, { "SetRespawnPos", valid_tile(world, tile) ? static_cast<u_int>(tile_index(tile)) : 0xffffffffu }, peer_of(p)->netid);
    }

    void end_game(::world &world, const std::string &why = "")
    {
        if (!world.battle.active) return;
        ::battle &b = world.battle;

        std::string result{};
        int best = -1, winners = 0, winner = 0;
        for (int t = 0; t < b.teams; ++t)
        {
            if (b.scores[t] > best) best = b.scores[t], winners = 1, winner = t;
            else if (b.scores[t] == best) ++winners;
        }
        result = (winners == 1) ? std::format("The {} won the game with `w{}`` point{}!", TEAM_NAMES[winner], best, (best == 1) ? "" : "s") : "It's a tie!";

        battle_event(world, 1, -1, 0); // @note everyone leaves their team, the music stops
        while (!b.changed.empty()) put_back(world, b.changed.back().first); // @note busted blocks rebuild, flags come back
        b.carried.clear();

        peers(world.name, PeerSearch::WORLD, [&](ENetPeer *p)
        {
            ::peer *pp = peer_of(p);
            send_varlist(p, { "OnSetMissionTimer", 0u, 0 });
            send_varlist(p, { "OnTextOverlay", std::format("`wGame over!`` {}", result) });
            on::ConsoleMessage(p, std::format("`5[``Game over! {}{}`5]``", result, why));
            if (member_team(world, pp->user_id) >= 0)
            {
                pp->rest_pos = world.spawn;
                send_respawn_tile(p, world, ::pos{-1, -1});
            }
        });
        peers(world.name, PeerSearch::WORLD, [](ENetPeer *p) { play_sfx(p, "audio/fanfare.wav"); });
        b = ::battle{};
    }

    void start_game(ENetEvent &event, ::world &world, const ::game_rules &rules)
    {
        ::battle &b = world.battle;
        b = ::battle{ .active = true, .generator = rules.pos, .mode = rules.mode, .teams = std::clamp<u_char>(rules.teams, 2, 4),
                      .goal = std::max<int>(rules.goal, 1), .ends = std::time(nullptr) + static_cast<long long>(std::clamp<int>(rules.minutes, 1, 30)) * 60 };

        /* @note players standing on a Staging Platform join its team first, the rest even out the teams */
        std::vector<ENetPeer*> waiting{};
        std::array<int, 4> size{};
        peers(world.name, PeerSearch::WORLD, [&](ENetPeer *p)
        {
            ::peer *pp = peer_of(p);
            const ::pos tile = center_tile(*pp);
            if (valid_tile(world, tile) && world.blocks[tile_index(tile)].fg == STAGING_PLATFORM && team_at(world, tile) < b.teams)
            {
                const u_char team = team_at(world, tile);
                b.members.emplace_back(pp->user_id, team);
                ++size[team];
            }
            else waiting.push_back(p);
        });
        for (ENetPeer *p : waiting)
        {
            const u_char team = static_cast<u_char>(std::min_element(size.begin(), size.begin() + b.teams) - size.begin());
            b.members.emplace_back(peer_of(p)->user_id, team);
            ++size[team];
        }

        /* @note the scoreboard: title, an unused u32, the teams and their scores */
        ::blob extra{};
        const std::string title = (b.mode == 0) ? "Capture the Flag!" : "Reach the Goal!";
        extra.i16(static_cast<short>(title.length()));
        for (char c : title) extra.u8(static_cast<u_char>(c));
        extra.u32(0);
        extra.u8(b.teams);
        for (int t = 0; t < b.teams; ++t) extra.i32(0);
        send_to_world(world, ::gamePacket{ .type = 0x1c, .netid = -1, .state = (int)gamePacketState::EXTENDED, .punch = ::pos{0, 0},
                                           .size = static_cast<u_int>(extra.size()) }, extra);

        const long long seconds = b.ends - std::time(nullptr);
        peers(world.name, PeerSearch::WORLD, [&](ENetPeer *p)
        {
            ::peer *pp = peer_of(p);
            const int team = member_team(world, pp->user_id);
            send_to_world(world, ::gamePacket{ .type = 0x1b, .netid = pp->netid, .punch = ::pos{team, 0} }); // @note BATTLE_JOIN
            send_varlist(p, { "OnSetMissionTimer", static_cast<u_int>(seconds), 1, std::format("First to {}", b.goal) });
            send_varlist(p, { "OnTextOverlay", std::format("`wThe game has started!`` You're on the {}!", TEAM_NAMES[team]) });
            const ::pos grave = grave_of(world, static_cast<u_char>(team));
            if (valid_tile(world, grave))
            {
                pp->rest_pos = grave.by_32(false);
                send_respawn_tile(p, world, grave);
                send_varlist(p, { "OnSetPos", CL_Vec2f{pp->rest_pos.x, pp->rest_pos.y} }, pp->netid);
            }
        });
        peers(world.name, PeerSearch::WORLD, [](ENetPeer *p) { play_sfx(p, "audio/race_start.wav"); });
    }

    void add_point(::world &world, ENetPeer *p, u_char team)
    {
        ::battle &b = world.battle;
        ++b.scores[team];
        battle_event(world, 3, -1, 1, team);           // @note team score
        battle_event(world, 2, peer_of(p)->netid, 1);  // @note player score
        if (b.scores[team] >= b.goal) end_game(world);
    }

    /* @brief a game player stepped on a tile */
    void game_step(ENetPeer *p, ::world &world, const ::pos &tile)
    {
        ::battle &b = world.battle;
        ::peer *pp = peer_of(p);
        const int team = member_team(world, pp->user_id);
        if (team < 0) return;
        const short fg = world.blocks[tile_index(tile)].fg;
        const u_char tile_team = team_at(world, tile);
        const bool carrying = std::ranges::find(b.carried, pp->user_id, &::battle::carry::uid) != b.carried.end();

        if (b.mode == 0 && fg == GAME_FLAG && tile_team != team && !carrying) // @note take an enemy (or neutral) flag
        {
            b.changed.emplace_back(tile, GAME_FLAG);
            b.carried.push_back({ pp->user_id, tile, tile_team });
            world.blocks[tile_index(tile)].fg = MISSING_GAME_FLAG;
            battle_event(world, 4, pp->netid, tile_index(tile)); // @note the game turns the flag into a Missing Game Flag itself
        }
        else if (fg == GAME_GOAL && tile_team == team)
        {
            if (b.mode == 0 && carrying)
            {
                drop_flag(world, pp->user_id, pp->netid, true);
                add_point(world, p, static_cast<u_char>(team));
            }
            else if (b.mode == 1)
            {
                add_point(world, p, static_cast<u_char>(team));
                if (world.battle.active) // @note back to the grave to go again
                    send_varlist(p, { "OnSetPos", CL_Vec2f{pp->rest_pos.x, pp->rest_pos.y} }, pp->netid, 300);
            }
        }
    }

    /* ---------------- Challenge Timer ---------------- */

    void challenge_stop(ENetPeer *p)
    {
        ::peer *pp = peer_of(p);
        pp->challenge.active = false;
        send_varlist(p, { "OnRaceEnd" }, pp->netid);
        send_varlist(p, { "OnCountdownEnd" }, pp->netid);
        send_varlist(p, { "OnSetMissionTimer", 0u, 0 });
    }

    void challenge_start(ENetPeer *p, ::world &world, const ::pos &start)
    {
        ::peer *pp = peer_of(p);
        const ::challenge_timer *timer = world.challenge_timers.empty() ? nullptr : &world.challenge_timers.front();
        int end = -1;
        for (std::size_t i = 0; i < world.blocks.size(); ++i) if (world.blocks[i].fg == CHALLENGE_END) { end = static_cast<int>(i); break; }
        if (end < 0)
        {
            send_varlist(p, { "OnTalkBubble", pp->netid, "There is no `wChallenge End Flag`` in this world!", 0u, 1u });
            return;
        }

        const long long now = now_ms();
        pp->challenge = { .active = true, .world = world.name, .start = now, .deadline = (timer != nullptr) ? now + timer->seconds * 1000ll : 0 };
        send_varlist(p, { "OnChallengeFlags", tile_index(start), end }, pp->netid);
        send_varlist(p, { "OnRaceStart" }, pp->netid);
        if (timer != nullptr)
        {
            send_varlist(p, { "OnCountdownStart", static_cast<u_int>(timer->seconds), 0 }, pp->netid);
            send_varlist(p, { "OnSetMissionTimer", static_cast<u_int>(timer->seconds), 1, "Challenge!" });
        }
        send_varlist(p, { "OnTextOverlay", (timer != nullptr) ? std::format("`wGo!`` Reach the End Flag in `w{}`` seconds!", timer->seconds) : "`wGo!`` Reach the End Flag!" });
        play_sfx(p, "audio/race_start.wav");
    }

    void challenge_finish(ENetPeer *p, ::world &world)
    {
        ::peer *pp = peer_of(p);
        const long long took = now_ms() - pp->challenge.start;
        challenge_stop(p);

        const std::string time = std::format("{}.{:02}", took / 1000, (took % 1000) / 10);
        std::string extra{};
        if (!world.challenge_timers.empty())
        {
            ::challenge_timer &timer = world.challenge_timers.front();
            if (timer.best_ms == 0 || took < timer.best_ms)
            {
                timer.best_ms = static_cast<int>(took), timer.best_name = pp->growid;
                extra = " `2That's a new world record!``";
            }
        }
        world_message(world.name, std::format("`5[```w{}`` finished the challenge in `w{}`` seconds!{}`5]``", pp->growid, time, extra));
        send_varlist(p, { "OnTextOverlay", std::format("`2Finished!`` `w{}`` seconds", time) });
        play_sfx(p, "audio/fanfare.wav");
    }

    /* ---------------- Safe Vault ---------------- */

    struct vault_session { std::string world{}; ::pos pos{}; long long until{}; };
    std::unordered_map<int, vault_session> open_vaults{}; // @note user id -> the vault they unlocked
    std::unordered_map<int, std::pair<int, long long>> vault_misses{}; // @note user id -> wrong passwords, since

    bool vault_unlocked(const ::peer &peer, const ::world &world, const ::pos &pos)
    {
        auto it = open_vaults.find(peer.user_id);
        return it != open_vaults.end() && it->second.world == world.name && it->second.pos == pos && it->second.until > now_ms();
    }

    bool valid_password(const std::string &text)
    {
        return text.length() >= 4 && text.length() <= 24 && std::ranges::all_of(text, [](unsigned char c) { return std::isalnum(c); });
    }

    void vault_contents(ENetEvent &event, ::world &world, const ::safe_vault &vault)
    {
        ::peer *pPeer = peer_of(event.peer);
        std::string rows{};
        for (std::size_t i = 0; i < vault.items.size(); ++i)
            rows += std::format("add_button_with_icon|take_{}||staticBlueFrame|{}|{}|\n", i, vault.items[i].id, vault.items[i].count);
        if (!rows.empty()) rows += "add_button_with_icon||END_LIST|noflags|0||\n";

        dialog(event.peer, std::format(
            "set_default_color|`o\n"
            "add_label_with_icon|big|`wSafe Vault``|left|8878|\n"
            "add_spacer|small|\n"
            "add_textbox|`w{}/{}`` stacks stored. Tap a stack to take it out.|left|\n"
            "{}"
            "add_spacer|small|\n"
            "add_item_picker|itemid|`wDeposit item``|Choose an item to lock in the vault!|\n"
            "{}"
            "embed_data|tilex|{}\n"
            "embed_data|tiley|{}\n"
            "end_dialog|vault|Close||\n",
            vault.items.size(), VAULT_STACKS, rows,
            is_editor(world, *pPeer, vault.pos) ? "add_button|change_password|`wChange password``|noflags|0|0|\n" : "",
            vault.pos.x_int(), vault.pos.y_int()));
    }

    void vault_password_dialog(ENetPeer *peer, const ::pos &pos, bool setting)
    {
        dialog(peer, std::format(
            "set_default_color|`o\n"
            "add_label_with_icon|big|`wSafe Vault``|left|8878|\n"
            "add_spacer|small|\n"
            "{}"
            "embed_data|tilex|{}\n"
            "embed_data|tiley|{}\n"
            "end_dialog|vault|Cancel|{}|\n",
            setting ?
                "add_textbox|Choose a password for this vault: 4 to 24 letters and numbers. Anyone who knows it can open the vault, so keep it secret!|left|\n"
                "add_text_input_password|new_password|Password||24|\n"
                "add_text_input_password|confirm_password|Again||24|\n" :
                "add_textbox|This vault is locked. Enter the password to open it.|left|\n"
                "add_text_input_password|password|Password||24|\n",
            pos.x_int(), pos.y_int(), setting ? "Set password" : "Open"));
    }
}

/* ---------------- shared ---------------- */

bool cursed(const ::peer &peer)
{
    return peer.cursed_until > std::time(nullptr);
}

std::string special_join_check(ENetEvent &event, const std::string &name)
{
    ::peer *pPeer = peer_of(event.peer);
    if (cursed(*pPeer) && name != "HELL")
    {
        const long long left = pPeer->cursed_until - std::time(nullptr);
        on::ConsoleMessage(event.peer, std::format("`4You're cursed!`` You can't leave HELL for `w{}`` more minute{}.", (left + 59) / 60, ((left + 59) / 60 == 1) ? "" : "s"));
        return "HELL";
    }
    auto world = std::ranges::find(worlds, name, &::world::name);
    if (world != worlds.end() && pPeer->role < MODERATOR)
    {
        auto ban = std::ranges::find(world->world_bans, pPeer->user_id, &::world_ban::uid);
        if (ban != world->world_bans.end() && ban->until > std::time(nullptr))
        {
            const long long left = ban->until - std::time(nullptr);
            throw std::runtime_error(std::format("`4Sorry, you're banned from {} for `w{}`` more minute{}.``", name, (left + 59) / 60, ((left + 59) / 60 == 1) ? "" : "s"));
        }
    }
    return "";
}

bool special_tile_change(ENetEvent &event, ::world &world, ::gamePacket &gamePacket, ::block &block)
{
    ::peer *pPeer = peer_of(event.peer);

    /* @note wands are used on the player standing on the tile */
    if (gamePacket.id == FREEZE_WAND || gamePacket.id == CURSE_WAND || gamePacket.id == BAN_WAND)
    {
        ENetPeer *target = nullptr;
        peers(world.name, PeerSearch::WORLD, [&](ENetPeer *p)
        {
            const ::peer *pp = peer_of(p);
            if (pp->user_id != pPeer->user_id && pp->pos.by_32(true) == gamePacket.punch) target = p;
            if (target == nullptr && pp->user_id != pPeer->user_id && center_tile(*pp) == gamePacket.punch) target = p;
        });
        if (target == nullptr) throw std::runtime_error("Must be used on a person.");
        ::peer *pTarget = peer_of(target);
        const bool world_boss = pPeer->owns(world.owner) || pPeer->role >= MODERATOR;
        if (pTarget->role >= MODERATOR && pPeer->role < pTarget->role) throw std::runtime_error("That doesn't work on staff!");
        if (gamePacket.id != FREEZE_WAND)
        {
            if (!world_boss) throw std::runtime_error("You can only use that in a world you own.");
            if (pTarget->user_id == world.owner) throw std::runtime_error("That doesn't work on the world's owner!");
        }
        modify_item_inventory(event, ::slot(gamePacket.id, -1));
        ENetEvent fake{ .peer = target };

        if (gamePacket.id == FREEZE_WAND)
        {
            pTarget->frozen_until = now_ms() + FREEZE_MS;
            pTarget->state |= S_FROZEN;
            on::SetClothing(target);
            world_varlist(world.name, { "OnSetFreezeState", 1u }, pTarget->netid);
            play_in_world(world.name, pTarget->netid, "audio/freeze.wav");
            world_message(world.name, std::format("`5[``{} `5froze`` {}`5!]``", pPeer->display_growid, pTarget->display_growid));
        }
        else if (gamePacket.id == CURSE_WAND)
        {
            pTarget->cursed_until = std::time(nullptr) + CURSE_SECONDS;
            pTarget->mysql_update("cursed_until", pTarget->cursed_until);
            world_message(world.name, std::format("`5[``{} `4cursed`` {}`5! They're on the highway to HELL!]``", pPeer->display_growid, pTarget->display_growid));
            on::ConsoleMessage(target, std::format("`4{} cursed you!`` You're stuck in HELL for 10 minutes.", pPeer->growid));
            action::quit_to_exit(fake, "", true);
            action::join_request(fake, "", "HELL");
        }
        else // @note Ban Wand
        {
            std::erase_if(world.world_bans, [&](const ::world_ban &ban) { return ban.uid == pTarget->user_id; });
            world.world_bans.push_back({ pTarget->user_id, std::time(nullptr) + BAN_SECONDS });
            world_message(world.name, std::format("`5[``{} was `4banned`` from {} by {} for 10 minutes`5]``", pTarget->display_growid, world.name, pPeer->display_growid));
            on::ConsoleMessage(target, std::format("`4You've been banned from {} for 10 minutes.``", world.name));
            play_in_world(world.name, pTarget->netid, "audio/repair.wav");
            const std::string name = world.name;
            action::quit_to_exit(fake, "", false);
            if (auto w = std::ranges::find(worlds, name, &::world::name); w != worlds.end()) w->save();
        }
        return true;
    }

    /* @note during a game a Game Block can be smashed by the other teams, and nothing else of the game can be broken */
    if (gamePacket.id == 18 && world.battle.active)
    {
        const ::item &item = id_to_item(block.fg);
        if (block.fg == GAME_BLOCK_ID)
        {
            const int team = member_team(world, pPeer->user_id);
            if (team < 0) throw std::runtime_error("Only players in the game can smash Game Blocks!");
            if (team == team_at(world, gamePacket.punch)) throw std::runtime_error("That's your own team's block!");
            if (block.hits[0] + 1 >= GAME_BLOCK_HITS)
            {
                world.battle.changed.emplace_back(gamePacket.punch, GAME_BLOCK_ID);
                block.fg = BUSTED_GAME_BLOCK, block.hits[0] = 0;
                world_tile_update(world, gamePacket.punch);
            }
            else tile_apply_damage(event, gamePacket, block, 0); // @note counts the hit
            return true;
        }
        if (item.type == type::GAME_BLOCK || item.type == type::GAME_GENERATOR)
            throw std::runtime_error("You can't break that during a game!");
    }
    return false;
}

std::string special_break_blocked(::world &world, const ::pos &pos, const ::item &item)
{
    if (item.type == type::SAFE_VAULT)
    {
        auto vault = std::ranges::find(world.vaults, pos, &::safe_vault::pos);
        if (vault != world.vaults.end() && !vault->items.empty()) return "Empty the vault first!";
    }
    return "";
}

void special_placed(::world &world, const ::pos &pos, const ::item &item)
{
    special_broken(world, pos);
    if (item.type == type::CHALLENGE_TIMER && world.challenge_timers.size() < 1) world.challenge_timers.push_back({ .pos = pos });
    if (item.type == type::GAME_GENERATOR) world.game_generators.push_back({ .pos = pos });
}

void special_broken(::world &world, const ::pos &pos)
{
    std::erase_if(world.krankens, [&](const ::kranken &k) { return k.pos == pos; });
    std::erase_if(world.vaults, [&](const ::safe_vault &v) { return v.pos == pos; });
    std::erase_if(world.game_tiles, [&](const ::game_tile &t) { return t.pos == pos; });
    std::erase_if(world.game_generators, [&](const ::game_rules &r) { return r.pos == pos; });
    std::erase_if(world.challenge_timers, [&](const ::challenge_timer &t) { return t.pos == pos; });
}

bool special_wrench(ENetEvent &event, ::world &world, const ::pos &pos, const ::item &item)
{
    ::peer *pPeer = peer_of(event.peer);
    const bool editor = is_editor(world, *pPeer, pos);
    switch (item.type)
    {
        case type::KRANKEN_GALACTIC:
        {
            if (!editor) throw std::runtime_error("You can't change this block's color here.");
            auto k = std::ranges::find(world.krankens, pos, &::kranken::pos);
            const u_int color = (k != world.krankens.end()) ? k->color : 0xffffffff;
            dialog(event.peer, ::create_dialog()
                .set_default_color("`o")
                .add_label_with_icon("big", std::format("`w{}``", item.raw_name), item.id)
                .add_textbox("Pick a color for this block (0 to 255 each). 255, 255, 255 shows its own colors.")
                .add_text_input("red", "Red", static_cast<short>((color >> 8) & 0xff), 3)
                .add_text_input("green", "Green", static_cast<short>((color >> 16) & 0xff), 3)
                .add_text_input("blue", "Blue", static_cast<short>((color >> 24) & 0xff), 3)
                .embed_data("tilex", pos.x_int())
                .embed_data("tiley", pos.y_int())
                .end_dialog("kranken", "Cancel", "Paint"));
            return true;
        }
        case type::SAFE_VAULT:
        {
            auto vault = std::ranges::find(world.vaults, pos, &::safe_vault::pos);
            if (vault == world.vaults.end() || vault->password.empty())
            {
                if (!editor) throw std::runtime_error("This Safe Vault hasn't been set up yet.");
                vault_password_dialog(event.peer, pos, true);
            }
            else if (vault_unlocked(*pPeer, world, pos)) vault_contents(event, world, *vault);
            else vault_password_dialog(event.peer, pos, false);
            return true;
        }
        case type::GAME_GENERATOR:
        {
            if (!editor) throw std::runtime_error("Only someone who can build here can set up the game.");
            auto r = std::ranges::find(world.game_generators, pos, &::game_rules::pos);
            if (r == world.game_generators.end()) r = world.game_generators.insert(world.game_generators.end(), ::game_rules{ .pos = pos });
            ::create_dialog d{};
            d.set_default_color("`o")
             .add_label_with_icon("big", "`wGame Generator``", item.id);
            if (world.battle.active)
            {
                const long long left = std::max(0ll, world.battle.ends - std::time(nullptr));
                std::string scores{};
                for (int t = 0; t < world.battle.teams; ++t) scores += std::format("{}: `w{}``  ", TEAM_NAMES[t], world.battle.scores[t]);
                d.add_textbox(std::format("A game is running: `w{}:{:02}`` left.", left / 60, left % 60))
                 .add_textbox(scores)
                 .add_button("end_game", "`4End the game now``");
            }
            else
            {
                d.add_textbox("Set the rules, then press Start. Everyone in the world joins: players standing on a Staging Platform get its team, the rest are shared out.")
                 .add_checkbox("ctf", "Capture the Flag (bring enemy Game Flags to your Game Goal). Untick: Reach the Goal (touch your team's Game Goal)", r->mode == 0)
                 .add_text_input("teams", "Teams (2-4)", static_cast<short>(r->teams), 1)
                 .add_text_input("minutes", "Minutes (1-30)", static_cast<short>(r->minutes), 2)
                 .add_text_input("goal", "Points to win (1-99)", static_cast<short>(r->goal), 2)
                 .add_button("start_game", "`2Start the game!``");
            }
            d.embed_data("tilex", pos.x_int()).embed_data("tiley", pos.y_int());
            dialog(event.peer, d.end_dialog("game_rules", "Close", world.battle.active ? "" : "Save"));
            return true;
        }
        case type::GAME_BLOCK:
        {
            if (!editor) throw std::runtime_error("Only someone who can build here can set the team.");
            if (world.battle.active) throw std::runtime_error("You can't change teams during a game!");
            const u_char team = team_at(world, pos);
            ::create_dialog d{};
            d.set_default_color("`o")
             .add_label_with_icon("big", std::format("`w{}``", item.raw_name), item.id)
             .add_textbox(std::format("This block belongs to: {}", TEAM_NAMES[std::min<u_char>(team, 4)]));
            for (int t = 0; t < 4; ++t) d.add_button(std::format("team_{}", t), TEAM_NAMES[t]);
            d.add_button("team_4", "No team")
             .embed_data("tilex", pos.x_int()).embed_data("tiley", pos.y_int());
            dialog(event.peer, d.end_dialog("game_tile", "Close", ""));
            return true;
        }
        case type::CHALLENGE_TIMER:
        {
            if (!editor) throw std::runtime_error("Only someone who can build here can set the timer.");
            auto t = std::ranges::find(world.challenge_timers, pos, &::challenge_timer::pos);
            if (t == world.challenge_timers.end()) t = world.challenge_timers.insert(world.challenge_timers.end(), ::challenge_timer{ .pos = pos });
            dialog(event.peer, ::create_dialog()
                .set_default_color("`o")
                .add_label_with_icon("big", "`wChallenge Timer``", item.id)
                .add_textbox("Players who touch the Challenge Start Flag get this long to reach the Challenge End Flag.")
                .add_text_input("seconds", "Seconds (5-900)", static_cast<short>(t->seconds), 3)
                .add_textbox((t->best_ms > 0) ? std::format("Best time: `w{}.{:02}`` seconds by `w{}``", t->best_ms / 1000, (t->best_ms % 1000) / 10, t->best_name) : "No one has finished yet.")
                .add_checkbox("reset_best", "Reset the best time", false)
                .embed_data("tilex", pos.x_int())
                .embed_data("tiley", pos.y_int())
                .end_dialog("challenge", "Cancel", "OK"));
            return true;
        }
        default: break;
    }
    return false;
}

void special_moved(ENetEvent &event)
{
    ::peer *pPeer = peer_of(event.peer);
    ::world *world = world_of(*pPeer);
    if (world == nullptr) return;

    static std::unordered_map<int, int> last_tile{}; // @note user id -> the tile they were in, so a flag only triggers when stepped onto
    const ::pos tile = center_tile(*pPeer);
    if (!valid_tile(*world, tile)) return;
    const int index = tile_index(tile);
    int &last = last_tile[pPeer->user_id];
    if (last == index) return;
    last = index;

    const short fg = world->blocks[index].fg;
    if (fg == CHALLENGE_START && !pPeer->challenge.active) challenge_start(event.peer, *world, tile);
    else if (fg == CHALLENGE_END && pPeer->challenge.active && pPeer->challenge.world == world->name) challenge_finish(event.peer, *world);

    if (world->battle.active) game_step(event.peer, *world, tile);
}

void special_respawn(ENetEvent &event)
{
    ::peer *pPeer = peer_of(event.peer);
    ::world *world = world_of(*pPeer);
    if (world == nullptr) return;
    if (pPeer->challenge.active) challenge_stop(event.peer), on::ConsoleMessage(event.peer, "`4Challenge failed!``");
    if (world->battle.active) drop_flag(*world, pPeer->user_id, pPeer->netid, false);
}

void special_left_world(ENetEvent &event, ::world &world)
{
    ::peer *pPeer = peer_of(event.peer);
    if (pPeer->challenge.active) challenge_stop(event.peer);
    open_vaults.erase(pPeer->user_id);
    if (!world.battle.active) return;
    drop_flag(world, pPeer->user_id, pPeer->netid, false);
    if (member_team(world, pPeer->user_id) >= 0)
    {
        battle_event(world, 6, pPeer->netid, 0); // @note leaves the team
        std::erase_if(world.battle.members, [&](const auto &m) { return m.first == pPeer->user_id; });
        pPeer->rest_pos = world.spawn;
        send_varlist(event.peer, { "OnSetMissionTimer", 0u, 0 });
    }
    if (world.visitors <= 1 || world.battle.members.empty()) end_game(world, " (everyone left)");
}

void special_entered(ENetEvent &event, ::world &world)
{
    if (!world.battle.active) return;
    on::ConsoleMessage(event.peer, "`5[``A game is running here. You can watch until it ends.`5]``");
}

void special_tick()
{
    static long long last{};
    const long long now = now_ms();
    if (now - last < 250) return;
    last = now;

    peers("", PeerSearch::EVERYONE, [now](ENetPeer *p)
    {
        ::peer *pp = peer_of(p);
        if (pp == nullptr || !pp->authenticated) return;

        if (pp->frozen_until != 0 && now >= pp->frozen_until)
        {
            pp->frozen_until = 0;
            pp->state &= ~S_FROZEN;
            if (pp->netid != 0)
            {
                on::SetClothing(p);
                world_varlist(pp->recent_worlds.back(), { "OnSetFreezeState", 0u }, pp->netid);
            }
        }
        if (pp->cursed_until != 0 && !cursed(*pp))
        {
            pp->cursed_until = 0;
            pp->mysql_update("cursed_until", 0ll);
            on::ConsoleMessage(p, "`2The curse has worn off!`` You can leave HELL now.");
            if (pp->netid != 0) on::SetClothing(p);
        }
        if (pp->challenge.active && pp->challenge.deadline != 0 && now >= pp->challenge.deadline)
        {
            challenge_stop(p);
            send_varlist(p, { "OnTextOverlay", "`4Time's up!`` Challenge failed." });
            play_sfx(p, "audio/bleep_fail.wav");
        }
    });

    for (::world &world : worlds)
        if (world.battle.active && std::time(nullptr) >= world.battle.ends) end_game(world, " (time's up)");
}

/* ---------------- dialog_return ---------------- */

void kranken_edit(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = peer_of(event.peer);
    ::world *world = world_of(*pPeer);
    ::pos pos{};
    if (world == nullptr || !read_tile(hPipe, *world, type::KRANKEN_GALACTIC, pos) || !is_editor(*world, *pPeer, pos)) return;

    const auto channel = [&](const char *key) { return static_cast<u_int>(std::clamp(atoi(hPipe[key].c_str()), 0, 255)); };
    const u_int color = (channel("red") << 8) | (channel("green") << 16) | (channel("blue") << 24) | 0xffu; // @note Proton's MAKE_RGBA
    auto k = std::ranges::find(world->krankens, pos, &::kranken::pos);
    if (k == world->krankens.end()) k = world->krankens.insert(world->krankens.end(), ::kranken{ .pos = pos });
    k->color = color;
    world_tile_update(*world, pos);
    world->save();
}

void vault_edit(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = peer_of(event.peer);
    ::world *world = world_of(*pPeer);
    ::pos pos{};
    if (world == nullptr || !read_tile(hPipe, *world, type::SAFE_VAULT, pos)) return;
    auto vault = std::ranges::find(world->vaults, pos, &::safe_vault::pos);
    const bool editor = is_editor(*world, *pPeer, pos);

    /* @note setting (or changing) the password */
    if (!hPipe["new_password"].empty() || !hPipe["confirm_password"].empty())
    {
        const bool has_password = vault != world->vaults.end() && !vault->password.empty();
        if (!editor || (has_password && !vault_unlocked(*pPeer, *world, pos))) return;
        const std::string pass = hPipe["new_password"];
        if (!valid_password(pass)) throw std::runtime_error("The password must be 4 to 24 letters and numbers.");
        if (pass != hPipe["confirm_password"]) throw std::runtime_error("The two passwords don't match.");
        if (vault == world->vaults.end()) vault = world->vaults.insert(world->vaults.end(), ::safe_vault{ .pos = pos });
        vault->password = auth::hash_password(pass);
        open_vaults[pPeer->user_id] = { world->name, pos, now_ms() + VAULT_OPEN_MS };
        on::ConsoleMessage(event.peer, "`2The vault's password is set.``");
        world->save();
        vault_contents(event, *world, *vault);
        return;
    }
    if (vault == world->vaults.end() || vault->password.empty()) return;

    /* @note opening it */
    if (!hPipe["password"].empty())
    {
        auto &[misses, since] = vault_misses[pPeer->user_id];
        if (now_ms() - since > 60'000) misses = 0, since = now_ms();
        if (misses >= 5) throw std::runtime_error("Too many wrong passwords. Wait a minute and try again.");
        if (!auth::verify_password(hPipe["password"], vault->password))
        {
            ++misses;
            play_sfx(event.peer, "audio/bleep_fail.wav");
            throw std::runtime_error("`4Wrong password!``");
        }
        open_vaults[pPeer->user_id] = { world->name, pos, now_ms() + VAULT_OPEN_MS };
        play_sfx(event.peer, "audio/door_open.wav");
        vault_contents(event, *world, *vault);
        return;
    }

    if (!vault_unlocked(*pPeer, *world, pos)) return;
    open_vaults[pPeer->user_id].until = now_ms() + VAULT_OPEN_MS;
    const std::string button = hPipe["buttonClicked"];

    if (button == "change_password")
    {
        if (editor) vault_password_dialog(event.peer, pos, true);
        return;
    }
    if (const short id = static_cast<short>(atoi(hPipe["itemid"].c_str())); id > 0 && hPipe["count"].empty())
    {
        const ::item &item = id_to_item(id);
        if ((item.cat & CAT_UNTRADEABLE) || item.type == type::FIST || item.type == type::WRENCH) throw std::runtime_error("That can't go in the vault.");
        const int have = pPeer->have_slot(::slot(id, 0));
        if (have <= 0) return;
        dialog(event.peer, ::create_dialog()
            .set_default_color("`o")
            .add_label_with_icon("big", std::format("`wDeposit {}``", item.raw_name), item.id)
            .add_text_input("count", "How many?", static_cast<short>(have), 5)
            .embed_data("itemid", id)
            .embed_data("vault_action", "deposit")
            .embed_data("tilex", pos.x_int())
            .embed_data("tiley", pos.y_int())
            .end_dialog("vault", "Cancel", "Deposit"));
        return;
    }
    if (button.starts_with("take_"))
    {
        const std::size_t index = static_cast<std::size_t>(atoi(button.substr(5).c_str()));
        if (index >= vault->items.size()) return;
        const ::slot &slot = vault->items[index];
        const ::item &item = id_to_item(slot.id);
        dialog(event.peer, ::create_dialog()
            .set_default_color("`o")
            .add_label_with_icon("big", std::format("`wWithdraw {}``", item.raw_name), item.id)
            .add_textbox(std::format("The vault holds `w{}``.", slot.count))
            .add_text_input("count", "How many?", slot.count, 5)
            .embed_data("itemid", slot.id)
            .embed_data("vault_action", "withdraw")
            .embed_data("tilex", pos.x_int())
            .embed_data("tiley", pos.y_int())
            .end_dialog("vault", "Cancel", "Withdraw"));
        return;
    }

    const short id = static_cast<short>(atoi(hPipe["itemid"].c_str()));
    if (id <= 0) return;
    if (hPipe["vault_action"] == "deposit")
    {
        const ::item &item = id_to_item(id);
        if ((item.cat & CAT_UNTRADEABLE) || item.type == type::FIST || item.type == type::WRENCH) return;
        const int count = std::min(atoi(hPipe["count"].c_str()), static_cast<int>(pPeer->have_slot(::slot(id, 0))));
        if (count <= 0) return;
        int left = count;
        for (::slot &slot : vault->items)
        {
            if (slot.id != id || slot.count >= 200) continue;
            const int add = std::min(left, 200 - slot.count);
            slot.count += add, left -= add;
        }
        while (left > 0 && static_cast<int>(vault->items.size()) < VAULT_STACKS)
        {
            const int add = std::min(left, 200);
            vault->items.emplace_back(id, static_cast<short>(add));
            left -= add;
        }
        const int stored = count - left;
        if (stored <= 0) throw std::runtime_error("The vault is full!");
        modify_item_inventory(event, ::slot(id, static_cast<short>(-stored)));
        on::ConsoleMessage(event.peer, std::format("`7[```9Locked {} {} in the vault.```7]``", stored, item.raw_name));
    }
    else if (hPipe["vault_action"] == "withdraw")
    {
        int stored = 0;
        for (const ::slot &slot : vault->items) if (slot.id == id) stored += slot.count;
        const int room = pPeer->can_hold(id, 1) ? 200 - pPeer->have_slot(::slot(id, 0)) : 0;
        int count = std::min({atoi(hPipe["count"].c_str()), stored, room});
        if (count <= 0) throw std::runtime_error((room <= 0) ? "You don't have room in your backpack!" : "There's nothing to take.");
        const int taken = count;
        for (auto it = vault->items.rbegin(); it != vault->items.rend() && count > 0; ++it)
        {
            if (it->id != id) continue;
            const int take = std::min(count, static_cast<int>(it->count));
            it->count -= take, count -= take;
        }
        std::erase_if(vault->items, [](const ::slot &slot) { return slot.count <= 0; });
        modify_item_inventory(event, ::slot(id, static_cast<short>(taken)));
        on::ConsoleMessage(event.peer, std::format("`7[```9Took {} {} out of the vault.```7]``", taken, id_to_item(id).raw_name));
    }
    else return;
    pPeer->mysql_update_all();
    world->save();
}

void game_rules_edit(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = peer_of(event.peer);
    ::world *world = world_of(*pPeer);
    ::pos pos{};
    if (world == nullptr || !read_tile(hPipe, *world, type::GAME_GENERATOR, pos) || !is_editor(*world, *pPeer, pos)) return;

    const std::string button = hPipe["buttonClicked"];
    if (button == "end_game")
    {
        end_game(*world, std::format(" (ended by {})", pPeer->growid));
        return;
    }
    if (world->battle.active) return;

    auto r = std::ranges::find(world->game_generators, pos, &::game_rules::pos);
    if (r == world->game_generators.end()) r = world->game_generators.insert(world->game_generators.end(), ::game_rules{ .pos = pos });
    if (!hPipe["teams"].empty())
    {
        r->mode    = (hPipe["ctf"] == "1") ? 0 : 1;
        r->teams   = static_cast<u_char>(std::clamp(atoi(hPipe["teams"].c_str()), 2, 4));
        r->minutes = static_cast<u_char>(std::clamp(atoi(hPipe["minutes"].c_str()), 1, 30));
        r->goal    = static_cast<u_char>(std::clamp(atoi(hPipe["goal"].c_str()), 1, 99));
        world->save();
    }
    if (button == "start_game")
    {
        if (r->mode == 0 && std::ranges::none_of(world->blocks, [](const ::block &b) { return b.fg == GAME_FLAG; }))
            throw std::runtime_error("Place some Game Flags first (wrench them to pick their team)!");
        if (std::ranges::none_of(world->blocks, [](const ::block &b) { return b.fg == GAME_GOAL; }))
            throw std::runtime_error("Place a Game Goal for each team first (wrench them to pick their team)!");
        start_game(event, *world, *r);
    }
}

void game_tile_edit(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = peer_of(event.peer);
    ::world *world = world_of(*pPeer);
    ::pos pos{};
    if (world == nullptr || !read_tile(hPipe, *world, type::GAME_BLOCK, pos) || !is_editor(*world, *pPeer, pos) || world->battle.active) return;
    const std::string button = hPipe["buttonClicked"];
    if (!button.starts_with("team_")) return;
    const u_char team = static_cast<u_char>(std::clamp(atoi(button.substr(5).c_str()), 0, 4));
    auto t = std::ranges::find(world->game_tiles, pos, &::game_tile::pos);
    if (t == world->game_tiles.end()) t = world->game_tiles.insert(world->game_tiles.end(), ::game_tile{ .pos = pos });
    t->team = team;
    world_tile_update(*world, pos);
    world->save();
}

void challenge_edit(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = peer_of(event.peer);
    ::world *world = world_of(*pPeer);
    ::pos pos{};
    if (world == nullptr || !read_tile(hPipe, *world, type::CHALLENGE_TIMER, pos) || !is_editor(*world, *pPeer, pos)) return;
    auto t = std::ranges::find(world->challenge_timers, pos, &::challenge_timer::pos);
    if (t == world->challenge_timers.end()) t = world->challenge_timers.insert(world->challenge_timers.end(), ::challenge_timer{ .pos = pos });
    t->seconds = std::clamp(atoi(hPipe["seconds"].c_str()), 5, 900);
    if (hPipe["reset_best"] == "1") t->best_ms = 0, t->best_name.clear();
    /* @note the first timer in the world is the one the flags use */
    std::iter_swap(world->challenge_timers.begin(), t);
    world->save();
}
