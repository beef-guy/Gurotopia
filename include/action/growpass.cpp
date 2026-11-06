#include "pch.hpp"

#include "onVariant/ConsoleMessage.hpp"
#include "onVariant/SetBux.hpp"
#include "tools/create_dialog.hpp"
#include "tools/sound.hpp"

#include <algorithm>

#include "growpass.hpp"

namespace
{
    constexpr int LEVELS           = 20;
    constexpr int POINTS_PER_LEVEL = 100;
    constexpr int LEVELS_PER_PAGE  = 5;
    constexpr int FREE_DAILY_GEMS  = 250;
    constexpr int ROYAL_DAILY_GEMS = 1000;
    constexpr int DAILY_POINTS     = 20;

    /* @note {item, count}; item 112 means gems */
    struct reward { short item; int count; };
    struct pass_level { reward free; reward royal; };
    constexpr short GEMS = 112;
    constexpr pass_level levels[LEVELS] = {
        {{GEMS, 500},   {5770, 1}},  // Growmoji World Lock
        {{834, 5},      {GEMS, 2500}},
        {{5706, 1},     {8970, 2}},  // Small Seed Pack, Bountiful Seed Pack
        {{GEMS, 1000},  {6744, 1}},  // Growmoji Alien
        {{242, 1},      {242, 5}},   // World Lock
        {{1488, 1},     {1488, 3}},  // Experience Potion
        {{228, 5},      {1778, 5}},  // Grow Spray Fertilizer, Deluxe Grow Spray
        {{GEMS, 1500},  {7234, 1}},  // Growmoji Nuke
        {{2480, 1},     {2480, 5}},  // Megaphone
        {{1486, 1},     {1486, 3}},  // Growtoken
        {{GEMS, 2000},  {GEMS, 7500}},
        {{822, 10},     {1062, 3}},  // Water Bucket, Ultra Grow Spray
        {{408, 3},      {1458, 1}},  // Duct Tape, Golden Heart Crystal
        {{242, 2},      {1796, 1}},  // World Lock, Diamond Lock
        {{GEMS, 2500},  {GEMS, 10000}},
        {{8970, 1},     {1486, 5}},  // Bountiful Seed Pack, Growtoken
        {{1488, 2},     {GEMS, 15000}},
        {{1486, 2},     {1796, 2}},  // Growtoken, Diamond Lock
        {{GEMS, 5000},  {6802, 1}},  // Mega Growtoken
        {{1796, 1},     {7188, 1}}   // Diamond Lock, Blue Gem Lock
    };

    std::tm utc_now()
    {
        const std::time_t now = std::time(nullptr);
        std::tm utc{};
#ifdef _WIN32
        gmtime_s(&utc, &now);
#else
        gmtime_r(&now, &utc);
#endif
        return utc;
    }
    int season() { const std::tm t = utc_now(); return (t.tm_year + 1900) * 100 + (t.tm_mon + 1); }
    int today() { const std::tm t = utc_now(); return (t.tm_year + 1900) * 10000 + (t.tm_mon + 1) * 100 + t.tm_mday; }

    std::string season_name()
    {
        static constexpr const char *months[] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
        const std::tm t = utc_now();
        return std::format("{} {}", months[t.tm_mon], t.tm_year + 1900);
    }

    /* @return "12 days" / "5 hours" left in this season */
    std::string season_left()
    {
        const std::tm t = utc_now();
        const int year = t.tm_year + 1900;
        const int days_in_month[] = { 31, ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0) ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
        const long long left = (static_cast<long long>(days_in_month[t.tm_mon] - t.tm_mday) * 86400) + (86400 - (t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec));
        if (left >= 86400) return std::format("{} day{}", left / 86400, (left / 86400 == 1) ? "" : "s");
        return std::format("{} hour{}", std::max(1ll, left / 3600), (left / 3600 <= 1) ? "" : "s");
    }

    std::string until_midnight()
    {
        const long long left = 86400ll - static_cast<long long>(std::time(nullptr) % 86400);
        return std::format("{}h {}m", left / 3600, (left % 3600) / 60);
    }

    int level_of(int points) { return std::min(points / POINTS_PER_LEVEL, LEVELS); }
    bool royal(const ::peer &peer) { return peer.pass_royal == season(); }

    std::string reward_text(const reward &r)
    {
        if (r.item == GEMS) return std::format("{} Gems", r.count);
        return std::format("{} {}", r.count, id_to_item(r.item).raw_name);
    }

    void save(::peer &peer)
    {
        peer.mysql_update("pass_season", peer.pass_season);
        peer.mysql_update("pass_points", peer.pass_points);
        peer.mysql_update("pass_free", peer.pass_free);
        peer.mysql_update("pass_royal_claims", peer.pass_royal_claims);
        peer.mysql_update("pass_royal", peer.pass_royal);
    }

    /* @return how many levels have a reward this peer could claim now */
    int unclaimed(const ::peer &peer)
    {
        int count = 0;
        for (int l = 0; l < level_of(peer.pass_points); ++l)
        {
            if (!(peer.pass_free & (1 << l))) ++count;
            if (royal(peer) && !(peer.pass_royal_claims & (1 << l))) ++count;
        }
        return count;
    }

    std::string header(const ::peer &peer)
    {
        const int level = level_of(peer.pass_points);
        std::string text = std::format(
            "set_default_color|`o\n"
            "add_label_with_icon|big|`wGrow Pass``{}|left|{}|\n"
            "add_smalltext|Season: `w{}`` (ends in `w{}``)|left|\n"
            "add_spacer|small|\n",
            royal(peer) ? " `6(Royal)``" : "", ROYAL_GROW_PASS_TOKEN, season_name(), season_left());
        if (level >= LEVELS)
            text += std::format("add_textbox|`2Level {} (max)!`` You have `w{}`` points this season.|left|\n", LEVELS, peer.pass_points);
        else
            text += std::format("add_textbox|Level `w{}``/{}  -  `w{}``/{} points to level {}|left|\n",
                level, LEVELS, peer.pass_points % POINTS_PER_LEVEL, POINTS_PER_LEVEL, level + 1);
        return text;
    }

    std::string tabs(const std::string &current)
    {
        auto tab = [&](const std::string &id, const std::string &label) {
            return std::format("add_button|tab_{}|{}{}``|noflags|0|0|\n", id, (id == current) ? "`2" : "`w", label);
        };
        return tab("rewards", "Rewards") + tab("daily", "Daily Bonus") + tab("points", "How to earn points") + tab("leaderboard", "Leaderboard") +
            "add_spacer|small|\n";
    }

    std::string rewards_page(const ::peer &peer, int page)
    {
        const int level = level_of(peer.pass_points);
        const bool is_royal = royal(peer);
        std::string text = std::format("embed_data|page|{}\n", page);
        const int pending = unclaimed(peer);
        if (pending > 0) text += std::format("add_textbox|`2You have {} reward{} to claim!`` Tap a reward to claim it.|left|\n", pending, (pending == 1) ? "" : "s");
        if (!is_royal) text += std::format("add_smalltext|`6Royal`` rewards (purple) need a `6Royal Grow Pass Token``.|left|\n");
        text += "add_spacer|small|\n";

        const int first = page * LEVELS_PER_PAGE;
        for (int l = first; l < std::min(first + LEVELS_PER_PAGE, LEVELS); ++l)
        {
            const bool reached = (l < level);
            const bool free_claimed = peer.pass_free & (1 << l), royal_claimed = peer.pass_royal_claims & (1 << l);
            text += std::format("add_label|small|{}Level {}``{}|left|\n", reached ? "`2" : "`w", l + 1,
                reached ? "" : std::format("  `o({} points)``", (l + 1) * POINTS_PER_LEVEL));
            auto button = [&](const char *kind, const reward &r, bool claimed, bool can, const char *frame) {
                const std::string label = claimed ? "`2Claimed``" : can ? "`wClaim!``" : (std::string_view(kind) == "royal" ? "`6Royal``" : "`oFree``");
                return std::format("add_button_with_icon|gp_{}_{}|{}|{}|{}|{}|\n", kind, l, label, frame, r.item, r.count);
            };
            text += button("free", levels[l].free, free_claimed, reached && !free_claimed, (reached && !free_claimed) ? "staticBlueFrame" : "staticGreyFrame");
            text += button("royal", levels[l].royal, royal_claimed, reached && is_royal && !royal_claimed, (reached && is_royal && !royal_claimed) ? "staticPurpleFrame" : "staticGreyFrame");
            text += "add_button_with_icon||END_LIST|noflags|0||\n";
        }
        text += "add_spacer|small|\n";
        if (page > 0) text += "add_button|page_prev|`wPrevious levels``|noflags|0|0|\n";
        if (first + LEVELS_PER_PAGE < LEVELS) text += "add_button|page_next|`wNext levels``|noflags|0|0|\n";
        return text;
    }

    std::string daily_page(const ::peer &peer)
    {
        const bool claimed = (peer.daily_bonus == today());
        std::string text = std::format(
            "add_textbox|Claim a bonus every day you play! It also gives `w{}`` Grow Pass points.|left|\n"
            "add_spacer|small|\n"
            "add_label_with_icon|small|{}Free: {} Gems``|left|112|\n"
            "add_label_with_icon|small|{}Royal: {} Gems``|left|{}|\n"
            "add_spacer|small|\n",
            DAILY_POINTS, royal(peer) ? "`o" : "`w", FREE_DAILY_GEMS, royal(peer) ? "`6" : "`o", ROYAL_DAILY_GEMS, ROYAL_GROW_PASS_TOKEN);
        if (claimed) text += std::format("add_textbox|`oYou already claimed today's bonus. Come back in `w{}``.``|left|\n", until_midnight());
        else text += std::format("add_button|claim_daily|`2Claim {} Gems``|noflags|0|0|\n", royal(peer) ? ROYAL_DAILY_GEMS : FREE_DAILY_GEMS);
        return text;
    }

    std::string points_page(const ::peer &peer)
    {
        return std::format(
            "add_textbox|Earn Grow Pass points by playing:|left|\n"
            "add_smalltext|`w1`` point for every 20 blocks you break|left|\n"
            "add_smalltext|`w1`` point for every 10 trees you harvest|left|\n"
            "add_smalltext|`w2`` points for every fish you catch|left|\n"
            "add_smalltext|`w15`` points for every Geiger Counter find|left|\n"
            "add_smalltext|`w15`` points for every fossil you prepare|left|\n"
            "add_smalltext|`w{}`` points for the daily bonus|left|\n"
            "add_smalltext|`w100`` points for Crazy Jim's daily quest|left|\n"
            "add_spacer|small|\n"
            "add_textbox|{}|left|\n",
            DAILY_POINTS,
            royal(peer) ? "`6You're Royal this season: every point counts double!``" :
                "`6Royal Grow Pass``: use a `6Royal Grow Pass Token`` to unlock the Royal rewards, double points and a bigger daily bonus until the season ends.");
    }

    std::string leaderboard_page(const ::peer &peer)
    {
        std::string text = "add_textbox|`wTop players this season``|left|\n";
        int rank = 0, my_rank = 0;
        {
            ::hStmt hStmt{ "SELECT growid, pass_points FROM peer WHERE pass_season = ? AND pass_points > 0 ORDER BY pass_points DESC, uid ASC LIMIT 10" };
            const int current = season();
            MYSQL_BIND params[1] = { make_bind_in(current) };
            hStmt.bind_param(params);

            std::string growid{};
            int points{};
            u_long growid_length{};
            MYSQL_BIND results[2] = { make_bind_out(growid), make_bind_out(points) };
            results[0].length = &growid_length;
            if (hStmt.pStmt != nullptr && mysql_stmt_bind_result(hStmt.pStmt, results) == 0)
            {
                hStmt.execute();
                while (mysql_stmt_fetch(hStmt.pStmt) == 0)
                {
                    ++rank;
                    const std::string name(growid.data(), std::min<u_long>(growid_length, growid.size()));
                    if (name == peer.growid) my_rank = rank;
                    const char *color = (rank == 1) ? "`9" : (rank == 2) ? "`w" : (rank == 3) ? "`6" : "`o";
                    text += std::format("add_smalltext|{}#{} {}`` - `w{}`` points (level {})|left|\n", color, rank, name, points, level_of(points));
                }
            }
        }
        if (rank == 0) text += "add_smalltext|Nobody has earned points this season yet. Be the first!|left|\n";
        text += "add_spacer|small|\n";
        text += (my_rank > 0) ?
            std::format("add_textbox|You are `2#{}`` with `w{}`` points.|left|\n", my_rank, peer.pass_points) :
            std::format("add_textbox|You have `w{}`` points this season.|left|\n", peer.pass_points);
        return text;
    }

    void claim_daily(ENetEvent &event)
    {
        ::peer *pPeer = static_cast<::peer*>(event.peer->data);
        if (pPeer->daily_bonus == today())
        {
            on::ConsoleMessage(event.peer, std::format("You already claimed today's bonus. Come back in {}.", until_midnight()));
            return;
        }
        const int gems = royal(*pPeer) ? ROYAL_DAILY_GEMS : FREE_DAILY_GEMS;
        pPeer->daily_bonus = today();
        pPeer->mysql_update("daily_bonus", pPeer->daily_bonus);
        pPeer->gems += gems;
        on::SetBux(event);
        pPeer->mysql_update_all();
        on::ConsoleMessage(event.peer, std::format("Daily bonus: you got `2{}`` Gems!", gems));
        send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, std::format("Daily bonus: `2{} Gems``!", gems), 0u, 1u });
        play_sfx(event.peer, "audio/piano_nice.wav");
        grow_pass_points(event.peer, DAILY_POINTS);
    }

    void claim_level(ENetEvent &event, bool is_royal_reward, int l)
    {
        ::peer *pPeer = static_cast<::peer*>(event.peer->data);
        if (l < 0 || l >= LEVELS) return;
        const reward &r = is_royal_reward ? levels[l].royal : levels[l].free;
        int &claims = is_royal_reward ? pPeer->pass_royal_claims : pPeer->pass_free;

        if (claims & (1 << l)) { on::ConsoleMessage(event.peer, "You already claimed that reward."); return; }
        if (level_of(pPeer->pass_points) <= l)
        {
            on::ConsoleMessage(event.peer, std::format("Reach `wlevel {}`` ({} points) to claim `w{}``.", l + 1, (l + 1) * POINTS_PER_LEVEL, reward_text(r)));
            return;
        }
        if (is_royal_reward && !royal(*pPeer))
        {
            on::ConsoleMessage(event.peer, std::format("`w{}`` is a `6Royal`` reward. Use a `6Royal Grow Pass Token`` to unlock it!", reward_text(r)));
            return;
        }
        if (r.item != GEMS && !pPeer->can_hold(r.item, static_cast<short>(r.count)))
        {
            on::ConsoleMessage(event.peer, "`4Make room in your backpack first!``");
            return;
        }
        claims |= (1 << l);
        save(*pPeer);
        if (r.item == GEMS)
        {
            pPeer->gems += r.count;
            on::SetBux(event);
        }
        else modify_item_inventory(event, ::slot(r.item, static_cast<short>(r.count)));
        pPeer->mysql_update_all();
        on::ConsoleMessage(event.peer, std::format("Grow Pass level {}: you got `2{}``!", l + 1, reward_text(r)));
        play_sfx(event.peer, "audio/piano_nice.wav");
    }
}

void grow_pass_season_check(::peer &peer)
{
    if (peer.pass_season == season()) return;
    peer.pass_season = season();
    peer.pass_points = 0;
    peer.pass_free = 0;
    peer.pass_royal_claims = 0;
    if (peer.user_id != 0) save(peer);
}

void grow_pass_points(ENetPeer *peer, int points)
{
    if (peer == nullptr || peer->data == nullptr || points <= 0) return;
    ::peer *pPeer = static_cast<::peer*>(peer->data);
    grow_pass_season_check(*pPeer);
    if (royal(*pPeer)) points *= 2;

    const int before = level_of(pPeer->pass_points);
    pPeer->pass_points += points;
    pPeer->mysql_update("pass_points", pPeer->pass_points);
    const int after = level_of(pPeer->pass_points);
    if (after > before)
    {
        on::ConsoleMessage(peer, std::format("`2Grow Pass level {}!`` You have a new reward to claim. Wrench yourself and open the Grow Pass.", after));
        send_varlist(peer, { "OnTalkBubble", pPeer->netid, std::format("`2Grow Pass level {}!``", after), 0u, 1u });
        play_sfx(peer, "audio/levelup2.wav");
    }
}

void grow_pass_break(ENetPeer *peer)
{
    ::peer *pPeer = static_cast<::peer*>(peer->data);
    if (++pPeer->pass_breaks >= 20) { pPeer->pass_breaks = 0; grow_pass_points(peer, 1); }
}

void grow_pass_harvest(ENetPeer *peer)
{
    ::peer *pPeer = static_cast<::peer*>(peer->data);
    if (++pPeer->pass_harvests >= 10) { pPeer->pass_harvests = 0; grow_pass_points(peer, 1); }
}

bool grow_pass_royal_token(ENetEvent &event)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    grow_pass_season_check(*pPeer);
    if (royal(*pPeer))
    {
        on::ConsoleMessage(event.peer, "You already have the `6Royal Grow Pass`` this season! Save the token for next season.");
        return false;
    }
    pPeer->pass_royal = season();
    save(*pPeer);
    on::ConsoleMessage(event.peer, std::format("`6You now have the Royal Grow Pass for {}!`` Royal rewards are unlocked and your points count double.", season_name()));
    send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, "`6Royal Grow Pass unlocked!``", 0u, 1u });
    play_sfx(event.peer, "audio/levelup2.wav");
    return true;
}

void show_grow_pass(ENetEvent &event, const std::string &tab_name)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    grow_pass_season_check(*pPeer);

    std::string tab = tab_name, body{};
    int page = 0;
    if (tab.starts_with("rewards"))
    {
        page = (tab.size() > 7) ? std::clamp(atoi(tab.substr(7).c_str()), 0, (LEVELS - 1) / LEVELS_PER_PAGE) : 0;
        tab = "rewards";
    }
    if (tab.empty()) // @note open on the page with the next thing to do
    {
        tab = "rewards";
        const int level = level_of(pPeer->pass_points);
        page = std::min(level, LEVELS - 1) / LEVELS_PER_PAGE;
        for (int l = 0; l < level; ++l)
            if (!(pPeer->pass_free & (1 << l)) || (royal(*pPeer) && !(pPeer->pass_royal_claims & (1 << l)))) { page = l / LEVELS_PER_PAGE; break; }
    }

    if (tab == "daily") body = daily_page(*pPeer);
    else if (tab == "points") body = points_page(*pPeer);
    else if (tab == "leaderboard") body = leaderboard_page(*pPeer);
    else body = rewards_page(*pPeer, page);

    send_varlist(event.peer, {
        "OnDialogRequest",
        header(*pPeer) + tabs(tab) + body +
        "add_spacer|small|\n"
        "add_quick_exit|\n"
        "end_dialog|grow_pass|Close||\n"
    });
}

void grow_pass_edit(ENetEvent &event, const ::hPipe &hPipe)
{
    const std::string button = hPipe["buttonClicked"];
    const int page = std::max(atoi(hPipe["page"].c_str()), 0);
    if (button.starts_with("tab_")) show_grow_pass(event, button.substr(4));
    else if (button == "page_next") show_grow_pass(event, std::format("rewards{}", page + 1));
    else if (button == "page_prev") show_grow_pass(event, std::format("rewards{}", std::max(page - 1, 0)));
    else if (button == "claim_daily")
    {
        claim_daily(event);
        show_grow_pass(event, "daily");
    }
    else if (button.starts_with("gp_free_") || button.starts_with("gp_royal_"))
    {
        const bool is_royal = button.starts_with("gp_royal_");
        const int l = atoi(button.substr(is_royal ? 9 : 8).c_str());
        claim_level(event, is_royal, l);
        show_grow_pass(event, std::format("rewards{}", std::clamp(l, 0, LEVELS - 1) / LEVELS_PER_PAGE));
    }
}

void passpoints_command(ENetEvent &event, const std::string_view text)
{
    const std::size_t space = text.find(' ');
    const int points = (space == std::string_view::npos) ? 0 : atoi(std::string(text.substr(space + 1)).c_str());
    if (points <= 0 || points > 100000) return on::ConsoleMessage(event.peer, "Usage: /passpoints <amount> (gives you Grow Pass points)");
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    grow_pass_season_check(*pPeer);
    const int before = level_of(pPeer->pass_points);
    pPeer->pass_points += points;
    pPeer->mysql_update("pass_points", pPeer->pass_points);
    on::ConsoleMessage(event.peer, std::format("You now have `w{}`` Grow Pass points (level {}).", pPeer->pass_points, level_of(pPeer->pass_points)));
    if (level_of(pPeer->pass_points) > before) play_sfx(event.peer, "audio/levelup2.wav");
}
