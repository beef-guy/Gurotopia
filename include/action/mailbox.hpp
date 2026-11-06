#pragma once

namespace action
{
    /* @note Growmail, the mailbox button on the left of the world screen */
    extern void show_mailbox_ui(ENetEvent& event, const std::string& header);
    extern void mailbox_getpage(ENetEvent& event, const std::string& header);
    extern void mailbox_readmessage(ENetEvent& event, const std::string& header);
    extern void mailbox_claimreward(ENetEvent& event, const std::string& header);
    extern void mailbox_claimrewardsmany(ENetEvent& event, const std::string& header);
}
