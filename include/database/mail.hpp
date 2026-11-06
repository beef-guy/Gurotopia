#pragma once

/*
* @note Growmail: the mailbox on the left of the world screen (MailboxButton).
*   mails are kept in the `mail` table. the client draws the inbox itself (show_mailbox_ui) and asks for it with
*   action|mailbox_getpage. claiming gives the rewards, which is how event prizes reach players, online or not.
*/

constexpr short MAIL_GEMS = 112; // @note a reward with this id is paid as gems

struct mail_row {
    int id{};
    int status{};          // @note 0 unread, 1 read or claimed
    int icon{};            // @note item shown next to the mail
    std::string title{}, body{};
    std::vector<std::pair<int, int>> rewards{}; // @note {item id, count}
    long long expires{};   // @note unix time
};

extern void mail_create_table();

/*
* @brief put a mail in `uid`'s mailbox. if they're online their mailbox button lights up.
* @param days how long it stays before it expires
*/
extern void mail_send(int uid, const std::string &title, const std::string &body, std::vector<std::pair<int, int>> rewards = {}, int icon = 0, int days = 30);

/* @return the mails that haven't expired, newest first */
extern std::vector<::mail_row> mail_list(int uid);

/* @return how many unread mails `uid` has */
extern int mail_unread(int uid);

/* @note refresh the mailbox button (lit when there's unread mail, with the count on it) */
extern void mail_button(ENetPeer *peer);

/* @return the mailbox button as JSON (also used in the login packet) */
extern std::string mail_button_json(int unread);

/* @note the owner can switch how a mail is packed for the client with /maillayout (see mail.cpp) */
extern std::string mail_layout;
/* @note how the rewards of a mail are written (1-8), changed with /mailrewards while we find the client format */
extern int mail_rewards;
