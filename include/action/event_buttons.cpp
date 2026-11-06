#include "pch.hpp"

#include "database/database.hpp"
#include "database/mail.hpp"
#include "onVariant/SetBux.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "tools/create_dialog.hpp"
#include "tools/sound.hpp"

#include <algorithm>
#include <ctime>

#include "event_buttons.hpp"

namespace
{
    constexpr short GEMS = 112, WORLD_LOCK = 242, DIAMOND_LOCK = 1796, GROWTOKEN = 1486;

    /* @note the seasonal buttons Gurotopia doesn't run yet. they stay hidden, as in the real game outside their event */
    constexpr std::string_view seasonal_buttons = R"json({"active":false,"buttonAction":"eventmenu","buttonTemplate":"BaseEventButton","counter":0,"counterMax":0,"itemIdIcon":6244,"name":"ClashEventButton","order":9,"rcssClass":"clash-event","text":""},{"active":false,"buttonAction":"showdungeonsui","buttonTemplate":"DungeonEventButton","counter":0,"counterMax":20,"name":"ScrollsPurchaseButton","order":30,"rcssClass":"scrollbank","text":""},{"active":false,"buttonAction":"show_auction_ui","buttonTemplate":"BaseEventButton","counter":0,"counterMax":0,"name":"AuctionButton","order":30,"rcssClass":"auction","text":""},{"active":false,"buttonTemplate":"ActiveAuctionEventButton","counter":0,"counterMax":20,"name":"ActiveAuctionButton","order":30,"rcssClass":"activeauction","text":""},{"active":false,"buttonAction":"eventmenu","buttonTemplate":"BaseEventButton","counter":0,"counterMax":0,"itemIdIcon":6244,"name":"ClashEventButton","order":21,"rcssClass":"clash-event","text":""},{"active":false,"buttonAction":"show_bingo_ui","buttonTemplate":"BaseEventButton","counter":0,"counterMax":0,"name":"WinterBingoButton","order":49,"rcssClass":"wf-bingo","text":""},{"active":false,"buttonAction":"show_bingo_ui","buttonTemplate":"BaseEventButton","name":"UbiBingoButton","order":50,"rcssClass":"ubi-bingo","text":""},{"active":false,"buttonAction":"winterrallymenu","buttonTemplate":"BaseEventButton","counter":0,"counterMax":0,"name":"WinterRallyButton","order":50,"rcssClass":"winter-rally","text":""},{"active":false,"buttonAction":"leaderboardBtnClicked","buttonTemplate":"BaseEventButton","counter":0,"counterMax":0,"name":"AnniversaryLeaderboardButton","order":50,"rcssClass":"anniversary-leaderboard","text":""},{"active":false,"buttonAction":"euphoriaBtnClicked","buttonTemplate":"BaseEventButton","counter":0,"counterMax":0,"name":"AnniversaryEuphoriaButton","order":50,"rcssClass":"anniversary-euphoria","text":""},{"active":false,"buttonAction":"openLnySparksPopup","buttonTemplate":"BaseEventButton","counter":0,"counterMax":5,"name":"LnyButton","order":50,"rcssClass":"cny","text":""},{"active":false,"buttonAction":"ShowValentinesQuestDialog","buttonTemplate":"EventButtonWithCounter","counter":0,"counterMax":100,"name":"ValentinesButton","order":50,"rcssClass":"valentines_day","text":""},{"active":false,"buttonAction":"showegseeventui","buttonTemplate":"EventButtonWithCounter","counter":0,"counterMax":20,"name":"EasterButton","order":50,"rcssClass":"easter_event","text":""},{"active":false,"buttonAction":"openStPatrickPiggyBank","buttonTemplate":"BaseEventButton","name":"StPatrickPBButton","order":50,"rcssClass":"st_patrick_event","text":""},{"active":false,"buttonAction":"dailyrewardmenu","buttonTemplate":"BaseEventButton","counter":0,"counterMax":1,"name":"CincoPinataButton","order":50,"rcssClass":"cinco_pinata_event","text":""},{"active":false,"buttonAction":"show_fruit_mixer_dialog","buttonTemplate":"BaseEventButton","counter":0,"counterMax":0,"name":"SPP_TropicalFruitsButton","order":50,"rcssClass":"spp_tropical_fruits","text":""},{"active":false,"buttonAction":"claimprogressbar","buttonTemplate":"EventButtonWithCounter","counter":0,"counterMax":0,"name":"SummerfestButton","order":50,"rcssClass":"summerfest","text":""},{"active":false,"buttonAction":"claimprogressbar","buttonTemplate":"EventButtonWithCounter","counter":0,"counterMax":15,"name":"HalloweenButton","order":50,"rcssClass":"halloween","text":""})json";

    /* @return "950", "12.5K", "1.5M" */
    std::string compact(long long n)
    {
        auto trim = [](double value, const char *unit)
        {
            std::string text = std::format("{:.1f}", value);
            if (text.ends_with(".0")) text.resize(text.size() - 2);
            return text + unit;
        };
        if (n >= 1'000'000) return trim(static_cast<double>(n) / 1'000'000.0, "M");
        if (n >= 1'000) return trim(static_cast<double>(n) / 1'000.0, "K");
        return std::to_string(n);
    }

    /* @return "1,234,567" */
    std::string commas(long long n)
    {
        std::string text = std::to_string(n);
        for (int i = static_cast<int>(text.size()) - 3; i > 0; i -= 3) text.insert(static_cast<std::size_t>(i), ",");
        return text;
    }

    /* ---------------- Piggy Bank ---------------- */

    int piggy_state(long long gems) { return (gems <= 0) ? 0 : (gems >= PIGGY_MAX) ? 2 : 1; }
    std::string piggy_label(long long gems) { return std::format("{}/{}", compact(gems), compact(PIGGY_MAX)); }

    std::string piggy_json(long long gems)
    {
        return std::format(
            "{{\"active\":true,\"buttonAction\":\"openPiggyBank\",\"buttonState\":{},\"buttonTemplate\":\"BaseEventButton\",\"counter\":0,\"counterMax\":0,\"itemIdIcon\":0,\"name\":\"PiggyBankButton\",\"notification\":{},\"order\":20,\"rcssClass\":\"piggybank\",\"text\":\"{}\"}}",
            piggy_state(gems), (gems >= PIGGY_MAX) ? -1 : 0, piggy_label(gems)
        ); // @note notification -1 puts the "!" on it once it's full
    }

    void piggy_button(ENetPeer *peer)
    {
        ::peer *pPeer = static_cast<::peer*>(peer->data);
        pPeer->piggy_label = piggy_label(pPeer->piggy_bank);
        send_varlist(peer, { "OnEventButtonDataSet", "PiggyBankButton", 1, piggy_json(pPeer->piggy_bank) });
    }

    /* ---------------- Daily Challenge ---------------- */

    /* @note what the challenge can be about: blocks everyone farms. {block id} (its seed is id + 1) */
    constexpr short challenge_items[] {
        4, 10, 14, 16, 52, 54, 56, 100, 102, 194, 180, 192, 340, 442, 526, 666, 846, 884, 954, 1004
    };
    constexpr int challenge_pool = sizeof(challenge_items) / sizeof(challenge_items[0]);

    /* @note prizes by place (1st, 2nd, ...). 4th to 10th all get the last one */
    const std::vector<std::vector<std::pair<int, int>>> challenge_prizes {
        { {GROWTOKEN, 3}, {GEMS, 25'000} },
        { {GROWTOKEN, 2}, {GEMS, 15'000} },
        { {GROWTOKEN, 1}, {GEMS, 10'000} },
        { {GEMS, 5'000} }
    };
    constexpr int CHALLENGE_WINNERS = 10;

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
    /* @return today as yyyymmdd (UTC). a new challenge starts at midnight UTC */
    int today() { const std::tm utc = utc_now(); return (utc.tm_year + 1900) * 10000 + (utc.tm_mon + 1) * 100 + utc.tm_mday; }
    long long seconds_left() { return 86400ll - static_cast<long long>(std::time(nullptr) % 86400); }

    short challenge_item(int day) { return challenge_items[(static_cast<u_int>(day) * 2246822519u >> 7) % challenge_pool]; }

    std::string time_left_text()
    {
        const long long left = seconds_left();
        if (left >= 3600) return std::format("{}h {}m", left / 3600, (left % 3600) / 60);
        return std::format("{}m", std::max(1ll, left / 60));
    }

    std::string challenge_json()
    {
        return std::format(
            "{{\"active\":true,\"buttonAction\":\"dailychallengemenu\",\"buttonState\":0,\"buttonTemplate\":\"BaseEventButton\",\"counter\":0,\"counterMax\":0,\"itemIdIcon\":{},\"name\":\"DailyChallenge\",\"notification\":0,\"order\":10,\"rcssClass\":\"daily_challenge\",\"text\":\"{}\"}}",
            challenge_item(today()), time_left_text()
        );
    }

    struct challenge_entry { int uid{}; std::string growid{}; int score{}; };

    std::vector<::challenge_entry> challenge_top(int day, int limit)
    {
        std::vector<::challenge_entry> rows{};
        ::hStmt hStmt{ "SELECT d.uid, p.growid, d.score FROM daily_challenge d JOIN peer p ON p.uid = d.uid WHERE d.day = ? AND d.score > 0 ORDER BY d.score DESC, d.reached ASC LIMIT ?" };
        MYSQL_BIND params[2] = { make_bind_in(day), make_bind_in(limit) };
        hStmt.bind_param(params);

        ::challenge_entry row{};
        std::string growid{};
        u_long length{};
        MYSQL_BIND results[3] = { make_bind_out(row.uid), make_bind_out(growid), make_bind_out(row.score) };
        results[1].length = &length;
        if (hStmt.pStmt == nullptr || mysql_stmt_bind_result(hStmt.pStmt, results)) return rows;

        hStmt.execute();
        while (mysql_stmt_fetch(hStmt.pStmt) == 0)
            rows.emplace_back(::challenge_entry{ row.uid, std::string(growid.data(), std::min<u_long>(length, growid.size())), row.score });
        return rows;
    }

    /* @return {score, place}. place is 0 with no score */
    std::pair<int, int> challenge_mine(int day, int uid)
    {
        int score{};
        {
            ::hStmt hStmt{ "SELECT score FROM daily_challenge WHERE day = ? AND uid = ?" };
            MYSQL_BIND params[2] = { make_bind_in(day), make_bind_in(uid) };
            hStmt.bind_param(params);
            MYSQL_BIND result = make_bind_out(score);
            if (hStmt.pStmt == nullptr || mysql_stmt_bind_result(hStmt.pStmt, &result)) return {0, 0};
            hStmt.execute();
            if (mysql_stmt_fetch(hStmt.pStmt) != 0) score = 0;
        }
        if (score <= 0) return {0, 0};

        long long ahead{};
        ::hStmt hStmt{ "SELECT COUNT(*) FROM daily_challenge WHERE day = ? AND score > ?" };
        MYSQL_BIND params[2] = { make_bind_in(day), make_bind_in(score) };
        hStmt.bind_param(params);
        MYSQL_BIND result = make_bind_out(ahead);
        if (hStmt.pStmt == nullptr || mysql_stmt_bind_result(hStmt.pStmt, &result)) return {score, 0};
        hStmt.execute();
        if (mysql_stmt_fetch(hStmt.pStmt) != 0) ahead = 0;
        return { score, static_cast<int>(ahead) + 1 };
    }

    void challenge_status(ENetPeer *peer)
    {
        const ::peer *pPeer = static_cast<::peer*>(peer->data);
        const int day = today();
        send_varlist(peer, { "OnDailyChallengeRequest", std::format("1|{}|{}|{}", challenge_mine(day, pPeer->user_id).first, seconds_left(), challenge_item(day)) });
    }

    std::string place_name(int place)
    {
        if (place % 100 >= 11 && place % 100 <= 13) return std::format("{}th", place);
        switch (place % 10) { case 1: return std::format("{}st", place); case 2: return std::format("{}nd", place); case 3: return std::format("{}rd", place); }
        return std::format("{}th", place);
    }

    std::string prize_text(const std::vector<std::pair<int, int>> &prize)
    {
        std::string text{};
        for (const auto &[id, count] : prize)
        {
            if (!text.empty()) text += " + ";
            text += (id == GEMS) ? std::format("{} Gems", commas(count)) : std::format("{} {}", count, id_to_item(static_cast<u_short>(id)).raw_name);
        }
        return text;
    }
    const std::vector<std::pair<int, int>> &prize_for(int place)
    {
        return challenge_prizes[std::min<std::size_t>(static_cast<std::size_t>(place - 1), challenge_prizes.size() - 1)];
    }
}

/* ---------------- shared ---------------- */

std::string event_buttons_json(const ::peer &peer)
{
    return std::format("{{\"EventButtonData\":[{},{},{},{}]}}",
        challenge_json(), piggy_json(peer.piggy_bank), mail_button_json(mail_unread(peer.user_id)), seasonal_buttons);
}

void event_buttons_refresh(ENetPeer *peer)
{
    send_varlist(peer, { "OnEventButtonDataSet", "DailyChallenge", 1, challenge_json() });
    piggy_button(peer);
    mail_button(peer);
    challenge_status(peer);
}

/* ---------------- Piggy Bank ---------------- */

void piggy_bank_add(ENetEvent &event, long long gems)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (gems <= 0 || pPeer->piggy_bank >= PIGGY_MAX) return;

    const bool was_full = pPeer->piggy_bank >= PIGGY_MAX;
    pPeer->piggy_bank = std::min(PIGGY_MAX, pPeer->piggy_bank + gems);
    pPeer->mysql_update("piggy_bank", pPeer->piggy_bank);

    if (!was_full && pPeer->piggy_bank >= PIGGY_MAX)
        on::ConsoleMessage(event.peer, "`2Your Piggy Bank is full!`` Smash it to collect the Gems inside.");
    if (piggy_label(pPeer->piggy_bank) != pPeer->piggy_label) piggy_button(event.peer); // @note only when the label changes
}

void action::openPiggyBank(ENetEvent& event, const std::string& header)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const long long gems = pPeer->piggy_bank;

    ::create_dialog dialog = ::create_dialog()
        .set_default_color("`o")
        .add_label_with_icon("big", "`wPiggy Bank``", GEMS)
        .add_spacer("small")
        .add_textbox(std::format("Your Piggy Bank holds `${}`` of `${}`` Gems.", commas(gems), commas(PIGGY_MAX)))
        .add_smalltext("Every time you collect Gems, the same amount drops into your Piggy Bank too. Smash it to take every Gem inside!")
        .add_spacer("small");

    if (gems >= PIGGY_MAX) dialog.add_textbox("`4It's full!`` It can't hold any more Gems until you smash it.");
    if (gems >= PIGGY_SMASH)
    {
        dialog.add_textbox(std::format("Smash it for `w{} Diamond Lock`` (or `w{} World Locks``) and get `${}`` Gems.", PIGGY_PRICE_DL, PIGGY_PRICE_DL * 100, commas(gems)))
              .add_button("smash", "`2Smash it!``");
    }
    else dialog.add_smalltext(std::format("It can be smashed once it holds `${}`` Gems.", commas(PIGGY_SMASH)));

    send_varlist(event.peer, { "OnDialogRequest", dialog.add_spacer("small").add_quick_exit().end_dialog("piggy_bank", "Close", "") });
}

void piggy_bank(ENetEvent& event, const ::hPipe &hPipe)
{
    if (hPipe["buttonClicked"] != "smash") return;
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    const long long gems = pPeer->piggy_bank;
    if (gems < PIGGY_SMASH) throw std::runtime_error("Your Piggy Bank doesn't have enough Gems to smash yet.");

    if (pPeer->have_slot(::slot(DIAMOND_LOCK, 0)) >= PIGGY_PRICE_DL) modify_item_inventory(event, ::slot(DIAMOND_LOCK, -PIGGY_PRICE_DL));
    else if (pPeer->have_slot(::slot(WORLD_LOCK, 0)) >= PIGGY_PRICE_DL * 100) modify_item_inventory(event, ::slot(WORLD_LOCK, static_cast<short>(-PIGGY_PRICE_DL * 100)));
    else throw std::runtime_error(std::format("You need {} Diamond Lock (or {} World Locks) to smash your Piggy Bank.", PIGGY_PRICE_DL, PIGGY_PRICE_DL * 100));

    pPeer->gems += gems;
    pPeer->piggy_bank = 0;
    pPeer->mysql_update_all();
    pPeer->mysql_update("piggy_bank", pPeer->piggy_bank);
    on::SetBux(event);
    piggy_button(event.peer);

    on::ConsoleMessage(event.peer, std::format("`2You smashed your Piggy Bank and got {} Gems!``", commas(gems)));
    send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, std::format("`wSmashed the Piggy Bank! `2+{} Gems``", commas(gems)), 0u, 1u });
    if (pPeer->netid != 0) play_in_world(pPeer->recent_worlds.back(), pPeer->netid, "audio/cash_register.wav");
    else play_sfx(event.peer, "audio/cash_register.wav");
}

/* ---------------- Daily Challenge ---------------- */

void daily_challenge_create_tables()
{
    mysql_query(db.mysql,
        "CREATE TABLE IF NOT EXISTS daily_challenge ("
        "day INT NOT NULL,"
        "uid INT NOT NULL,"
        "score INT DEFAULT 0,"
        "reached BIGINT DEFAULT 0,"
        "PRIMARY KEY (day, uid)"
    ")"); // @note reached: when the score last went up, so the first to get there wins a tie
    mysql_query(db.mysql,
        "CREATE TABLE IF NOT EXISTS daily_challenge_paid ("
        "day INT NOT NULL PRIMARY KEY"
    ")");
}

void daily_challenge_harvest(ENetEvent &event, short block_id)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const int day = today();
    if (block_id != challenge_item(day) || pPeer->user_id == 0) return;

    {
        const long long now = std::time(nullptr);
        ::hStmt hStmt{ "INSERT INTO daily_challenge (day, uid, score, reached) VALUES (?, ?, 1, ?) ON DUPLICATE KEY UPDATE score = score + 1, reached = VALUES(reached)" };
        MYSQL_BIND params[3] = { make_bind_in(day), make_bind_in(pPeer->user_id), make_bind_in(now) };
        hStmt.bind_param(params);
        hStmt.execute();
    }
    challenge_status(event.peer);
}

void daily_challenge_settle()
{
    const int day_now = today();
    std::vector<int> days{};
    {
        ::hStmt hStmt{ "SELECT DISTINCT d.day FROM daily_challenge d LEFT JOIN daily_challenge_paid p ON p.day = d.day WHERE d.day < ? AND p.day IS NULL" };
        MYSQL_BIND param = make_bind_in(day_now);
        hStmt.bind_param(&param);
        int day{};
        MYSQL_BIND result = make_bind_out(day);
        if (hStmt.pStmt == nullptr || mysql_stmt_bind_result(hStmt.pStmt, &result)) return;
        hStmt.execute();
        while (mysql_stmt_fetch(hStmt.pStmt) == 0) days.emplace_back(day);
    }

    for (int day : days)
    {
        {
            ::hStmt hStmt{ "INSERT IGNORE INTO daily_challenge_paid (day) VALUES (?)" };
            MYSQL_BIND param = make_bind_in(day);
            hStmt.bind_param(&param);
            hStmt.execute();
            if (hStmt.pStmt == nullptr || mysql_stmt_affected_rows(hStmt.pStmt) != 1) continue; // @note someone else paid it already
        }
        const ::item &item = id_to_item(static_cast<u_short>(challenge_item(day)));
        int place = 0;
        for (const ::challenge_entry &entry : challenge_top(day, CHALLENGE_WINNERS))
        {
            ++place;
            mail_send(entry.uid,
                std::format("Daily Challenge: {} place!", place_name(place)),
                std::format("You harvested {} {} Trees and finished in {} place in the Daily Challenge. Here's your prize!", entry.score, item.raw_name, place_name(place)),
                prize_for(place), item.id);
        }
    }
}

void action::dailychallengemenu(ENetEvent& event, const std::string& header)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    daily_challenge_settle();

    const int day = today();
    const ::item &item = id_to_item(static_cast<u_short>(challenge_item(day)));
    const auto [score, place] = challenge_mine(day, pPeer->user_id);

    ::create_dialog dialog = ::create_dialog()
        .set_default_color("`o")
        .add_label_with_icon("big", "`wDaily Challenge``", item.id)
        .add_spacer("small")
        .add_textbox(std::format("Today's challenge: harvest the most `2{} Trees``!", item.raw_name))
        .add_smalltext(std::format("Time left: `w{}``. A new challenge starts every day at midnight (UTC).", time_left_text()))
        .add_spacer("small")
        .add_textbox((score > 0) ?
            std::format("You've harvested `w{}`` so far, that's `w{}`` place.", score, place_name(place)) :
            std::string("You haven't harvested any yet. Plant some seeds and get going!"))
        .add_spacer("small")
        .add_label_with_icon("small", "`wTop 10``", 1486);

    const std::vector<::challenge_entry> top = challenge_top(day, CHALLENGE_WINNERS);
    if (top.empty()) dialog.add_smalltext("Nobody has scored yet today. It could be you!");
    int rank = 0;
    for (const ::challenge_entry &entry : top)
    {
        ++rank;
        dialog.add_smalltext(std::format("{}{}. {}`` - {}", (entry.uid == pPeer->user_id) ? "`2" : "`w", rank, entry.growid, entry.score));
    }

    dialog.add_spacer("small")
          .add_label_with_icon("small", "`wPrizes``", GROWTOKEN);
    for (std::size_t i = 0; i + 1 < challenge_prizes.size(); ++i)
        dialog.add_smalltext(std::format("{} place: `2{}``", place_name(static_cast<int>(i) + 1), prize_text(challenge_prizes[i])));
    dialog.add_smalltext(std::format("{} to {} place: `2{}``", place_name(static_cast<int>(challenge_prizes.size())), place_name(CHALLENGE_WINNERS), prize_text(challenge_prizes.back())))
          .add_smalltext("Prizes are sent to your `wMailbox`` when the day ends.");

    send_varlist(event.peer, { "OnDialogRequest", dialog.add_spacer("small").add_quick_exit().end_dialog("daily_challenge", "Close", "") });
    challenge_status(event.peer);
}
