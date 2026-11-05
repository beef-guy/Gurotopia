#include "pch.hpp"

#include "onVariant/TradeStatus.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "commands/moderation.hpp"

#include "trade.hpp"

#include <algorithm>

namespace
{
    ENetPeer *find_user(int user_id)
    {
        ENetPeer *found = nullptr;
        if (user_id <= 0) return found;
        peers("", PeerSearch::EVERYONE, [&](ENetPeer *peer)
        {
            const ::peer *pOthers = static_cast<::peer*>(peer->data);
            if (found == nullptr && pOthers != nullptr && pOthers->authenticated && pOthers->user_id == user_id) found = peer;
        });
        return found;
    }

    void reset(::peer &peer)
    {
        peer.trade_offers.clear();
        peer.trade_accepted = false;
        peer.trade_confirmed = false;
        peer.trade_with = 0;
        peer.trade_request = 0;
    }

    void unlock(::peer &peer)
    {
        peer.trade_accepted = false;
        peer.trade_confirmed = false;
    }

    /* @return how many new backpack slots `peer` needs to take `incoming` after giving `outgoing`. -1 if a stack would go over 200 */
    int slots_needed(const ::peer &peer, const std::vector<::slot> &outgoing, const std::vector<::slot> &incoming)
    {
        int needed = 0;
        for (const ::slot &in : incoming)
        {
            auto it = std::ranges::find(peer.slots, in.id, &::slot::id);
            int have = (it != peer.slots.end()) ? it->count : 0;
            if (auto out = std::ranges::find(outgoing, in.id, &::slot::id); out != outgoing.end()) have -= out->count;
            if (have + in.count > 200) return -1;
            if (it == peer.slots.end()) ++needed; // @note a slot left at 0 is reused
        }
        return needed;
    }

    int free_slots(const ::peer &peer)
    {
        return peer.slot_size - static_cast<int>(peer.slots.size());
    }

    /* @return true if every offered item is still in the backpack */
    bool still_has(::peer &peer)
    {
        for (const ::slot &offer : peer.trade_offers)
            if (offer.count <= 0 || peer.have_slot(::slot(offer.id, 0)) < offer.count) return false;
        return true;
    }

    std::string offer_lines(const std::vector<::slot> &offers)
    {
        std::string lines{};
        for (const ::slot &offer : offers)
            lines += std::format("add_label_with_icon|small|(`w{}``) {}|left|{}|\n", offer.count, id_to_item(offer.id).raw_name, offer.id);
        if (lines.empty()) lines = "add_label|small|Nothing|left|\n";
        return lines;
    }

    void send_confirm(ENetPeer *to, const ::peer &self, const ::peer &other)
    {
        send_varlist(to, {
            "OnDialogRequest",
            std::format(
                "set_default_color|`o\n"
                "add_label_with_icon|big|`wTrade Confirmation``|left|1366|\n"
                "add_spacer|small|\n"
                "add_label|small|`4You'll give:``|left|\n"
                "{}"
                "add_spacer|small|\n"
                "add_label|small|`2You'll get from {}:``|left|\n"
                "{}"
                "add_spacer|small|\n"
                "end_dialog|trade_confirm|Cancel|Do The Deal!|\n",
                offer_lines(self.trade_offers), other.display_growid, offer_lines(other.trade_offers)
            )
        });
    }

    void finish(ENetPeer *a, ENetPeer *b)
    {
        ::peer *pA = static_cast<::peer*>(a->data);
        ::peer *pB = static_cast<::peer*>(b->data);

        const auto fail = [&](const std::string &why)
        {
            for (ENetPeer *p : {a, b})
            {
                on::ConsoleMessage(p, std::format("`4The trade was canceled: {}``", why));
                send_varlist(p, { "OnForceTradeEnd" });
            }
            reset(*pA), reset(*pB);
        };

        if (!still_has(*pA) || !still_has(*pB)) return fail("someone doesn't have the items any more.");
        const int need_a = slots_needed(*pA, pA->trade_offers, pB->trade_offers);
        const int need_b = slots_needed(*pB, pB->trade_offers, pA->trade_offers);
        if (need_a < 0 || need_b < 0) return fail("a stack would go over 200.");
        if (need_a > free_slots(*pA)) return fail(std::format("{} doesn't have enough backpack space.", pA->growid));
        if (need_b > free_slots(*pB)) return fail(std::format("{} doesn't have enough backpack space.", pB->growid));

        /* @note copy the offers, the swap below clears nothing but keeps this readable */
        const std::vector<::slot> from_a = pA->trade_offers, from_b = pB->trade_offers;
        ENetEvent event_a{}, event_b{};
        event_a.peer = a, event_b.peer = b;

        for (const ::slot &s : from_a) modify_item_inventory(event_a, ::slot(s.id, -s.count));
        for (const ::slot &s : from_b) modify_item_inventory(event_b, ::slot(s.id, -s.count));
        for (const ::slot &s : from_b) modify_item_inventory(event_a, ::slot(s.id, s.count));
        for (const ::slot &s : from_a) modify_item_inventory(event_b, ::slot(s.id, s.count));

        std::string detail{};
        for (const ::slot &s : from_a) detail += std::format("{}x{} ", s.id, s.count);
        detail += "for ";
        for (const ::slot &s : from_b) detail += std::format("{}x{} ", s.id, s.count);
        moderation::log(*pA, "trade", pB->growid, detail);

        /* @note save both right away, so a crash can't undo half a trade */
        pA->mysql_update_all();
        pB->mysql_update_all();

        for (ENetPeer *p : {a, b})
        {
            const ::peer *other = (p == a) ? pB : pA;
            on::ConsoleMessage(p, std::format("`1** Trade with {} complete! **``", other->display_growid));
            send_varlist(p, { "OnForceTradeEnd" });
            send_gameMessage(p, {.message = "action|play_sfx\nfile|audio/keypad_hit.wav\ndelayMS|0"});
        }
        reset(*pA), reset(*pB);
    }
}

ENetPeer *trade::partner(const ::peer &peer)
{
    ENetPeer *other = find_user(peer.trade_with);
    if (other == nullptr) return nullptr;
    const ::peer *pOther = static_cast<::peer*>(other->data);
    if (pOther->trade_with != peer.user_id || pOther->netid == 0 || peer.netid == 0) return nullptr;
    if (pOther->recent_worlds.back() != peer.recent_worlds.back()) return nullptr;
    return other;
}

void trade::request(ENetEvent &event, ENetPeer *other)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::peer *pOther = static_cast<::peer*>(other->data);
    if (other == event.peer || pPeer->netid == 0 || pOther->netid == 0) return;

    if (pPeer->trade_with != 0 && pPeer->trade_with != pOther->user_id) trade::cancel(event.peer);

    pPeer->trade_offers.clear();
    unlock(*pPeer);
    pPeer->trade_with = pOther->user_id;
    send_varlist(event.peer, { "OnStartTrade", pOther->display_growid, pOther->netid });

    if (pOther->trade_with == pPeer->user_id) // @note they asked first, so the trade starts now
    {
        trade::changed(event);
        return;
    }
    on::ConsoleMessage(event.peer, std::format("`#Waiting for {} to trade back...``", pOther->display_growid));
    on::ConsoleMessage(other, std::format("`#TRADE ALERT:`` {} wants to trade with you! To start, wrench them and press `wTrade``.", pPeer->display_growid));
    send_gameMessage(other, {.message = "action|play_sfx\nfile|audio/cash_register.wav\ndelayMS|0"});
}

void trade::changed(ENetEvent &event)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ENetPeer *other = trade::partner(*pPeer);

    unlock(*pPeer);
    on::TradeStatus(event.peer, *pPeer);
    if (other == nullptr) return;

    ::peer *pOther = static_cast<::peer*>(other->data);
    unlock(*pOther);
    on::TradeStatus(event.peer, *pOther);
    on::TradeStatus(other, *pPeer);
    on::TradeStatus(other, *pOther);
    send_varlist(other, { "OnTextOverlay", "The deal has changed" });
}

void trade::cancel(ENetPeer *peer, const std::string &message)
{
    ::peer *pPeer = static_cast<::peer*>(peer->data);
    if (pPeer == nullptr || pPeer->trade_with == 0) return;

    if (ENetPeer *other = find_user(pPeer->trade_with); other != nullptr)
    {
        ::peer *pOther = static_cast<::peer*>(other->data);
        if (pOther->trade_with == pPeer->user_id)
        {
            reset(*pOther);
            send_varlist(other, { "OnForceTradeEnd" });
            on::ConsoleMessage(other, message.empty() ? std::format("`4{} canceled the trade.``", pPeer->display_growid) : message);
        }
    }
    reset(*pPeer);
}

void action::trade_accept(ENetEvent& event, const std::string& header)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ENetPeer *other = trade::partner(*pPeer);
    if (other == nullptr) return;
    ::peer *pOther = static_cast<::peer*>(other->data);

    const ::hPipe hPipe{ header };
    pPeer->trade_accepted = (hPipe["status"] == "1");
    pPeer->trade_confirmed = false;

    on::TradeStatus(event.peer, *pPeer);
    on::TradeStatus(other, *pPeer);

    if (pPeer->trade_accepted && pOther->trade_accepted)
    {
        if (pPeer->trade_offers.empty() && pOther->trade_offers.empty()) return; // @note nothing to trade
        send_confirm(event.peer, *pPeer, *pOther);
        send_confirm(other, *pOther, *pPeer);
    }
}

void action::trade_cancel(ENetEvent& event, const std::string& header)
{
    trade::cancel(event.peer);
}

void trade_confirm(ENetEvent& event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ENetPeer *other = trade::partner(*pPeer);
    if (other == nullptr || !pPeer->trade_accepted) return;
    ::peer *pOther = static_cast<::peer*>(other->data);
    if (!pOther->trade_accepted) return;

    pPeer->trade_confirmed = true;
    if (!pOther->trade_confirmed)
    {
        on::ConsoleMessage(event.peer, std::format("`oWaiting for {} to confirm...``", pOther->display_growid));
        return;
    }
    finish(other, event.peer);
}
