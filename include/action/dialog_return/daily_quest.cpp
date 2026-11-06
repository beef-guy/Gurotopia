#include "pch.hpp"
#include "action/growpass.hpp"

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

void show_daily_quest(ENetEvent& event, const ::pos *phone)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const auto [first, second] = todays_quest();
    const ::item &one = id_to_item(first.first), &two = id_to_item(second.first);
    const int have_one = pPeer->have_slot(::slot(first.first, 0)), have_two = pPeer->have_slot(::slot(second.first, 0));

    ::create_dialog dialog = ::create_dialog()
        .set_default_color("`o")
        .add_label_with_icon("big", "`wCrazy Jim's Quest Emporium``", CRAZY_JIM);

    if (pPeer->daily_quest == today())
    {
        dialog.add_textbox("You've already completed my Daily Quest for today! Call me back after midnight to hear about my next cravings.")
              .add_spacer("small");
    }
    else
    {
        dialog.add_textbox("I guess some people call me Crazy Jim because I'm a bit of a hoarder. But I'm very particular about what I want! And today, what I want is this:")
              .add_spacer("small")
              .add_label_with_icon("small", std::format("`2{} {}``", first.second, one.raw_name), one.id)
              .add_smalltext("and")
              .add_label_with_icon("small", std::format("`2{} {}``", second.second, two.raw_name), two.id)
              .add_spacer("small")
              .add_textbox("You shove all that through the phone (it works, I've tried it), and I will hand you one of the `2Growtokens`` from my personal collection!  But hurry, this offer is only good until midnight, and only one `2Growtoken`` per person!")
              .add_spacer("small")
              .add_textbox(std::format("You have {} {} and {} {}", have_one, one.raw_name, have_two, two.raw_name))
              .add_spacer("small");
        if (have_one >= first.second && have_two >= second.second) dialog.add_button("deliver", "`2Turn in quest items``");
    }
    if (phone != nullptr)
    {
        dialog.add_button("jim_menu", "Back")
              .embed_data("tilex", phone->x_int())
              .embed_data("tiley", phone->y_int());
        send_varlist(event.peer, { "OnDialogRequest", dialog.end_dialog("telephone", "Hang Up", "") });
        return;
    }
    send_varlist(event.peer, { "OnDialogRequest", dialog.add_quick_exit().end_dialog("daily_quest", "Hang Up", "") });
}

void deliver_daily_quest(ENetEvent& event)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (pPeer->daily_quest == today()) return;

    const auto [first, second] = todays_quest();
    if (pPeer->have_slot(::slot(first.first, 0)) < first.second || pPeer->have_slot(::slot(second.first, 0)) < second.second)
        throw std::runtime_error("I don't have enough items to turn it in!");
    if (!pPeer->can_hold(GROWTOKEN, 1))
        throw std::runtime_error("This seems far-fetched, but you've already reached the maximum amount of Growtokens.");

    modify_item_inventory(event, ::slot(first.first, -first.second));
    modify_item_inventory(event, ::slot(second.first, -second.second));
    modify_item_inventory(event, ::slot(GROWTOKEN, 1));
    pPeer->daily_quest = today();
    pPeer->mysql_update_all(); // @note save now, so a crash or restart can't hand out a second Growtoken today

    send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, "`wThanks, pardner! Have 1 `2Growtoken`w!", 0u, 1u });
    on::ConsoleMessage(event.peer, std::format("[`6You jammed {} {} and {} {} into the phone, and 1 `2Growtoken`` popped out!``]",
        first.second, id_to_item(first.first).raw_name, second.second, id_to_item(second.first).raw_name));
    if (pPeer->netid != 0) play_in_world(pPeer->recent_worlds.back(), pPeer->netid, "audio/piano_nice.wav");
    else play_sfx(event.peer, "audio/piano_nice.wav");
    grow_pass_points(event.peer, 100);
}

void daily_quest(ENetEvent& event, const ::hPipe &hPipe)
{
    if (hPipe["buttonClicked"] == "deliver") deliver_daily_quest(event);
}
