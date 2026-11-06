#include "pch.hpp"

#include "database.hpp"
#include "friends.hpp" // @note online_by_uid()
#include "onVariant/ConsoleMessage.hpp"
#include "tools/sound.hpp"

#include <algorithm>

#include "mail.hpp"

std::string mail_layout = "id,status,icon,0,expires,0"; // @note spots 0, 1, 2, 7, 8, 9 of a packed mail

namespace
{
    std::string rewards_to_text(const std::vector<std::pair<int, int>> &rewards)
    {
        std::string text{};
        for (const auto &[id, count] : rewards)
        {
            if (!text.empty()) text += ',';
            text += std::format("{}:{}", id, count);
        }
        return text;
    }

    std::vector<std::pair<int, int>> rewards_from_text(const std::string &text)
    {
        std::vector<std::pair<int, int>> rewards{};
        for (const std::string &part : readch(text, ','))
        {
            const std::size_t colon = part.find(':');
            if (colon == std::string::npos) continue;
            const int id = atoi(part.substr(0, colon).c_str()), count = atoi(part.substr(colon + 1).c_str());
            if (id > 0 && count > 0) rewards.emplace_back(id, count);
        }
        return rewards;
    }

    std::string cut(const std::string &buffer, u_long length)
    {
        return std::string(buffer.data(), std::min<u_long>(length, buffer.size()));
    }
}

void mail_create_table()
{
    mysql_query(db.mysql,
        "CREATE TABLE IF NOT EXISTS mail ("
        "id INT AUTO_INCREMENT PRIMARY KEY,"
        "uid INT NOT NULL,"
        "status INT DEFAULT 0,"
        "icon INT DEFAULT 0,"
        "title VARCHAR(128) DEFAULT '',"
        "body VARCHAR(1000) DEFAULT '',"
        "rewards VARCHAR(255) DEFAULT '',"
        "created BIGINT DEFAULT 0,"
        "expires BIGINT DEFAULT 0,"
        "INDEX (uid)"
    ")");
}

void mail_send(int uid, const std::string &title, const std::string &body, std::vector<std::pair<int, int>> rewards, int icon, int days)
{
    if (uid == 0) return;
    if (icon == 0) icon = rewards.empty() ? 242/*World Lock*/ : rewards.front().first;

    const long long now = std::time(nullptr), expires = now + static_cast<long long>(days) * 86400ll;
    const std::string rewards_text = rewards_to_text(rewards);
    const std::string short_title = title.substr(0, 128), short_body = body.substr(0, 1000);
    {
        ::hStmt hStmt{ "INSERT INTO mail (uid, status, icon, title, body, rewards, created, expires) VALUES (?, 0, ?, ?, ?, ?, ?, ?)" };
        MYSQL_BIND params[7] = {
            make_bind_in(uid), make_bind_in(icon), make_bind_in(short_title), make_bind_in(short_body),
            make_bind_in(rewards_text), make_bind_in(now), make_bind_in(expires)
        };
        hStmt.bind_param(params);
        hStmt.execute();
    } // @note close the statement before reading the count back

    if (ENetPeer *peer = online_by_uid(uid); peer != nullptr)
    {
        on::ConsoleMessage(peer, "`2You've got mail!`` Tap the `wMailbox`` on the left of your screen to read it.");
        play_sfx(peer, "audio/secret.wav");
        mail_button(peer);
    }
}

std::vector<::mail_row> mail_list(int uid)
{
    std::vector<::mail_row> rows{};
    if (uid == 0) return rows;

    const long long now = std::time(nullptr);
    ::hStmt hStmt{ "SELECT id, status, icon, title, body, rewards, expires FROM mail WHERE uid = ? AND expires > ? AND status < 2 ORDER BY id DESC LIMIT 100" };
    MYSQL_BIND params[2] = { make_bind_in(uid), make_bind_in(now) };
    hStmt.bind_param(params);

    ::mail_row row{};
    std::string title{}, body{}, rewards{};
    u_long title_length{}, body_length{}, rewards_length{};
    MYSQL_BIND results[7] = {
        make_bind_out(row.id), make_bind_out(row.status), make_bind_out(row.icon),
        make_bind_out(title), make_bind_out(body), make_bind_out(rewards), make_bind_out(row.expires)
    };
    results[3].length = &title_length;
    results[4].length = &body_length;
    results[5].length = &rewards_length;
    if (hStmt.pStmt == nullptr || mysql_stmt_bind_result(hStmt.pStmt, results)) return rows;

    hStmt.execute();
    while (mysql_stmt_fetch(hStmt.pStmt) == 0)
    {
        ::mail_row copy = row;
        copy.title = cut(title, title_length);
        copy.body = cut(body, body_length);
        copy.rewards = rewards_from_text(cut(rewards, rewards_length));
        rows.emplace_back(std::move(copy));
    }
    return rows;
}

int mail_unread(int uid)
{
    return static_cast<int>(std::ranges::count_if(mail_list(uid), [](const ::mail_row &row) { return row.status == 0; }));
}

std::string mail_button_json(int unread)
{
    return std::format(
        "{{\"active\":true,\"buttonAction\":\"show_mailbox_ui\",\"buttonState\":{},\"buttonTemplate\":\"BaseEventButton\",\"counter\":0,\"counterMax\":0,\"itemIdIcon\":0,\"name\":\"MailboxButton\",\"notification\":{},\"order\":30,\"rcssClass\":\"mailbox\",\"text\":\"\"}}",
        (unread > 0) ? 1 : 0, unread
    );
}

void mail_button(ENetPeer *peer)
{
    const ::peer *pPeer = static_cast<::peer*>(peer->data);
    if (pPeer == nullptr || !pPeer->authenticated) return;
    send_varlist(peer, { "OnEventButtonDataSet", "MailboxButton", 1, mail_button_json(mail_unread(pPeer->user_id)) });
}
