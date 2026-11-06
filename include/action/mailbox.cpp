#include "pch.hpp"

#include "database/database.hpp"
#include "database/mail.hpp"
#include "onVariant/SetBux.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "tools/sound.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>

#include "mailbox.hpp"

namespace
{
    /* @note just enough MessagePack (https://msgpack.org) for the mail page */
    struct msgpack
    {
        std::string out{};

        void array(std::size_t size)
        {
            if (size < 16) out += static_cast<char>(0x90 | size);
            else { out += static_cast<char>(0xdc); out += static_cast<char>((size >> 8) & 0xff); out += static_cast<char>(size & 0xff); }
        }
        void integer(long long value)
        {
            if (value >= 0 && value < 128) { out += static_cast<char>(value); return; }
            if (value < 0 && value >= -32) { out += static_cast<char>(value); return; }
            if (value >= INT32_MIN && value <= INT32_MAX)
            {
                out += static_cast<char>(0xd2); // @note int 32
                for (int shift = 24; shift >= 0; shift -= 8) out += static_cast<char>((value >> shift) & 0xff);
                return;
            }
            out += static_cast<char>(0xd3); // @note int 64
            for (int shift = 56; shift >= 0; shift -= 8) out += static_cast<char>((value >> shift) & 0xff);
        }
        void string(const std::string &text)
        {
            const std::size_t size = text.size();
            if (size < 32) out += static_cast<char>(0xa0 | size);
            else if (size < 256) { out += static_cast<char>(0xd9); out += static_cast<char>(size); }
            else { out += static_cast<char>(0xda); out += static_cast<char>((size >> 8) & 0xff); out += static_cast<char>(size & 0xff); }
            out += text;
        }
    };

    std::string rewards_json(const ::mail_row &row)
    {
        std::string json = "{";
        for (const auto &[id, count] : row.rewards)
        {
            if (json.size() > 1) json += ',';
            json += std::format("\"{}\":{}", id, count);
        }
        return json + "}";
    }

    /*
    * @note a mail is an array of 13 values. 3-5 are the texts, 6 the rewards, 8 the expiry time, 10-12 the text
    *   parameters. what the plain numbers 0, 1, 2, 7, 8 and 9 hold isn't known for sure (the id, status and icon are
    *   among them), so they follow `mail_layout`: six names (id, status, icon, expires, left, or a number) for those
    *   six spots. the owner can change it in game with /maillayout.
    */
    void pack_mail(msgpack &pack, const ::mail_row &row)
    {
        const std::vector<std::string> layout = readch(mail_layout, ',');
        auto value = [&](std::size_t i) -> long long
        {
            const std::string name = (i < layout.size()) ? layout[i] : "0";
            if (name == "id") return row.id;
            if (name == "status") return row.status;
            if (name == "icon") return row.icon;
            if (name == "left") return std::max(0ll, row.expires - static_cast<long long>(std::time(nullptr)));
            if (name == "expires") return row.expires;
            return atoll(name.c_str());
        };

        pack.array(13);
        pack.integer(value(0));
        pack.integer(value(1));
        pack.integer(value(2));
        pack.string(row.title);
        pack.string(row.body.substr(0, 60)); // @note the short text in the list
        pack.string(row.body);
        pack.string(rewards_json(row));
        pack.integer(value(3));
        pack.integer(value(4));
        pack.integer(value(5));
        pack.string("");
        pack.string("");
        pack.string("");
    }

    /* @return true if every reward fits, then hands them over */
    bool give_rewards(ENetEvent &event, const ::mail_row &row)
    {
        ::peer *pPeer = static_cast<::peer*>(event.peer->data);
        for (const auto &[id, count] : row.rewards)
            if (id != MAIL_GEMS && !pPeer->can_hold(static_cast<short>(id), count)) return false;

        for (const auto &[id, count] : row.rewards)
        {
            if (id == MAIL_GEMS)
            {
                pPeer->gems += count;
                on::SetBux(event);
                continue;
            }
            int left = count;
            while (left > 0)
            {
                const short now = static_cast<short>(std::min(left, 200));
                modify_item_inventory(event, ::slot(static_cast<short>(id), now));
                left -= now;
            }
            on::ConsoleMessage(event.peer, std::format("`2You got {} `w{}`` from your mail.``", count, id_to_item(static_cast<u_short>(id)).raw_name));
        }
        pPeer->mysql_update_all(); // @note save now so a crash can't pay a mail twice
        return true;
    }

    void set_status(int id, int status)
    {
        ::hStmt hStmt{ "UPDATE mail SET status = ? WHERE id = ?" };
        MYSQL_BIND params[2] = { make_bind_in(status), make_bind_in(id) };
        hStmt.bind_param(params);
        hStmt.execute();
    }

    /* @return the mail, if it's this player's and still in the box */
    std::optional<::mail_row> find_mail(int uid, int id)
    {
        for (::mail_row &row : mail_list(uid)) if (row.id == id) return row;
        return std::nullopt;
    }

    /* @return 1 if the mail was claimed (or had nothing to claim) */
    int claim(ENetEvent &event, int id)
    {
        ::peer *pPeer = static_cast<::peer*>(event.peer->data);
        std::optional<::mail_row> row = find_mail(pPeer->user_id, id);
        if (!row) return 0;
        if (row->status != 0) return 1; // @note already claimed

        if (!row->rewards.empty() && !give_rewards(event, *row))
        {
            on::ConsoleMessage(event.peer, "`4Your backpack is too full to claim that mail. Make some room and try again.``");
            return 0;
        }
        set_status(row->id, 1);
        return 1;
    }
}

void action::show_mailbox_ui(ENetEvent& event, const std::string& header)
{
    send_varlist(event.peer, { "OnDialogRequestRML", "show_mailbox_ui", "" }); // @note opens the client's own mailbox screen
}

void action::mailbox_getpage(ENetEvent& event, const std::string& header)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const int page = atoi(::hPipe{ header }["pageIndex"].c_str());

    /* @note every mail goes out in one page. a request for a later page means the client scrolled to the end: nothing more */
    if (pPeer->mail_page_sent && page > pPeer->mail_page)
    {
        send_varlist(event.peer, { "OnRequestMailPage", 0, "", page, 1 });
        return;
    }
    pPeer->mail_page_sent = true;
    pPeer->mail_page = page;

    const std::vector<::mail_row> rows = mail_list(pPeer->user_id);
    const int unread = static_cast<int>(std::ranges::count_if(rows, [](const ::mail_row &row) { return row.status == 0; }));
    if (rows.empty())
    {
        send_varlist(event.peer, { "OnRequestMailPage", 0, "", page, 1 });
        return;
    }

    msgpack pack{};
    pack.array(3);
    pack.array(rows.size());
    for (const ::mail_row &row : rows) pack_mail(pack, row);
    pack.integer(unread);
    pack.integer(static_cast<long long>(rows.size()));

    send_varlist(event.peer, { "OnRequestMailPage", 1, pack.out, page, 1 });
}

void action::mailbox_readmessage(ENetEvent& event, const std::string& header)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const int id = atoi(::hPipe{ header }["messageID"].c_str());

    std::optional<::mail_row> row = find_mail(pPeer->user_id, id);
    if (row && row->status == 0 && row->rewards.empty()) set_status(row->id, 1); // @note mails with rewards are read by claiming them
    send_varlist(event.peer, { "OnRequestReadMailMsg" });
    mail_button(event.peer);
}

void action::mailbox_claimreward(ENetEvent& event, const std::string& header)
{
    const int id = atoi(::hPipe{ header }["messageID"].c_str());
    const int ok = claim(event, id);
    if (ok) play_sfx(event.peer, "audio/cash_register.wav");
    send_varlist(event.peer, { "OnRequestClaimMailReward", ok });
    mail_button(event.peer);
}

void action::mailbox_claimrewardsmany(ENetEvent& event, const std::string& header)
{
    const std::string list = ::hPipe{ header }["listIDs"];
    int ok = 1, claimed = 0;
    for (const std::string &part : readch(list, ','))
    {
        if (part.empty()) continue;
        if (claim(event, atoi(part.c_str())) == 0) ok = 0;
        else ++claimed;
    }
    if (claimed > 0) play_sfx(event.peer, "audio/cash_register.wav");
    send_varlist(event.peer, { "OnRequestClaimRewardsMany", ok });
    mail_button(event.peer);
}
