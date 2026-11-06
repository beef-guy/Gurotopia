#include "pch.hpp"

#include "onVariant/ConsoleMessage.hpp"
#include "tools/create_dialog.hpp"
#include "tools/sound.hpp"

#include <ctime>

#include "daily_quest.hpp"

namespace
{
    constexpr short GROWTOKEN = 1486;
    constexpr short CRAZY_JIM = 3902;

    /* @note what Crazy Jim may ask for: common farmables, {item, amount} */
    constexpr std::pair<short, short> wants[] {
        {4, 200}, {10, 200}, {14, 200}, {16, 200}, {22, 200}, {52, 200}, {54, 200}, {56, 200}, {100, 200}, {102, 200},
        {442, 200}, {194, 200}, {884, 200}, {180, 150}, {192, 150}, {1004, 150}, {666, 100}, {846, 100}, {954, 100},
        {526, 100}, {5666, 50}, {4584, 50}, {340, 40}, {928, 20}
    };
    constexpr int pool = sizeof(wants) / sizeof(wants[0]);

    /* @return today as yyyymmdd (UTC) */
    int today()
    {
        const std::time_t now = std::time(nullptr);
        std::tm utc{};
#ifdef _WIN32
        gmtime_s(&utc, &now);
#else
        gmtime_r(&now, &utc);
#endif
        return (utc.tm_year + 1900) * 10000 + (utc.tm_mon + 1) * 100 + utc.tm_mday;
    }

    /* @return today's two wants. everyone gets the same quest on the same day */
    std::pair<std::pair<short, short>, std::pair<short, short>> todays_quest()
    {
        const u_int day = static_cast<u_int>(today());
        const u_int a = (day * 2654435761u) % pool;
        u_int b = ((day + 7u) * 40503u) % pool;
        if (b == a) b = (b + 1) % pool;
        return { wants[a], wants[b] };
    }
}

void show_daily_quest(ENetEvent& event)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const auto [first, second] = todays_quest();
    const ::item &one = id_to_item(first.first), &two = id_to_item(second.first);
    const int have_one = pPeer->have_slot(::slot(first.first, 0)), have_two = pPeer->have_slot(::slot(second.first, 0));

    ::create_dialog dialog = ::create_dialog()
        .set_default_color("`o")
        .add_label_with_icon("big", "`wCrazy Jim's Daily Quest``", CRAZY_JIM)
        .add_spacer("small")
        .add_textbox("I guess some people call me Crazy Jim because I'm a bit of a hoarder. But I'm very particular about what I want! And today, what I want is this:")
        .add_label_with_icon("small", std::format("`w{} {}``", first.second, one.raw_name), one.id)
        .add_smalltext("and")
        .add_label_with_icon("small", std::format("`w{} {}``", second.second, two.raw_name), two.id)
        .add_spacer("small")
        .add_textbox("You shove all that through the phone (it works, I've tried it), and I will hand you one of the `2Growtokens`` from my personal collection!  But hurry, this offer is only good until midnight, and only one `2Growtoken`` per person!")
        .add_spacer("small");

    if (pPeer->daily_quest == today())
        dialog.add_textbox("`2You've already done today's quest. Come back tomorrow!``");
    else
    {
        dialog.add_smalltext(std::format("`6(You have {} {} and {} {}.)``", have_one, one.raw_name, have_two, two.raw_name));
        if (have_one >= first.second && have_two >= second.second) dialog.add_button("deliver", "`wDeliver!``");
    }
    send_varlist(event.peer, { "OnDialogRequest", dialog.add_quick_exit().end_dialog("daily_quest", "Hang Up", "") });
}

void daily_quest(ENetEvent& event, const ::hPipe &hPipe)
{
    if (hPipe["buttonClicked"] != "deliver") return;

    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (pPeer->daily_quest == today()) return;

    const auto [first, second] = todays_quest();
    if (pPeer->have_slot(::slot(first.first, 0)) < first.second || pPeer->have_slot(::slot(second.first, 0)) < second.second) return;
    if (!pPeer->can_hold(GROWTOKEN, 1))
    {
        on::ConsoleMessage(event.peer, "`4Oops:`` `oYou don't have room in your backpack for a `2Growtoken``.``");
        return;
    }

    modify_item_inventory(event, ::slot(first.first, -first.second));
    modify_item_inventory(event, ::slot(second.first, -second.second));
    modify_item_inventory(event, ::slot(GROWTOKEN, 1));
    pPeer->daily_quest = today();
    pPeer->mysql_update_all(); // @note save now, so a crash or restart can't hand out a second Growtoken today

    on::ConsoleMessage(event.peer, "`9[Crazy Jim]`` `oThanks, pal! Here's your `2Growtoken``. Come back tomorrow for a new quest!``");
    play_sfx(event.peer, "audio/piano_nice.wav");
}
