#include "pch.hpp"

#include "https/server_data.hpp"
#include "tools/auth.hpp"

#include "protocol.hpp"

void action::protocol(ENetEvent& event, const std::string& header)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    bool is_reg = false;
    try
    {
        std::vector<std::string> pipes = readch(header, '|');
        if (pipes.size() < 4ull) throw std::runtime_error("");

        if (pipes[2ull] == "ltoken")
        {
            const std::string &decoded = base64_decode(pipes[3ull]);
            if (decoded.empty()) throw std::runtime_error("");

            if (std::size_t pos = decoded.find("growId="); pos != std::string::npos)
            {
                pos += sizeof("growId=") - 1;
                pPeer->growid = decoded.substr(pos, decoded.find('&', pos) - pos);
            }
            if (std::size_t pos = decoded.find("password="); pos != std::string::npos)
            {
                pos += sizeof("password=") - 1;
                pPeer->password = decoded.substr(pos, decoded.find('&', pos) - pos);
            }
            if (std::size_t pos = decoded.find("reg="); pos != std::string::npos)
            {
                pos += sizeof("reg=") - 1;
                std::string reg_str = decoded.substr(pos, decoded.find('&', pos) - pos);
                is_reg = (reg_str == "1");
            }
        } // @note delete decoded
        if (pPeer->growid.empty() || pPeer->password.empty()) throw std::runtime_error("");
    }
    catch (...) {
        send_gameMessage(event.peer, {.message = "action|logon_fail\n"}); // @note triggers action|quit on client.
        
        return; // @note stop processing invalid protocol data
    }
    if (auth::login_blocked(event.peer->address))
    {
        send_gameMessage(event.peer, {.message = "action|log\nmsg|`4Too many wrong passwords.`` Please wait a few minutes and try again."});
        send_gameMessage(event.peer, {.message = "action|logon_fail\n"}); // @note triggers action|quit on client.
        return;
    }
    bool loaded = false;
    if (is_reg)
    {
        loaded = pPeer->create_account(pPeer->growid, pPeer->password);
    }
    else {
        loaded = pPeer->load(pPeer->growid, pPeer->password);
    }
    pPeer->password.clear(); // @note don't keep the password around once it's checked
    if (!loaded)
    {
        if (!is_reg) auth::login_failed(event.peer->address);
        pPeer->authenticated = false;
        send_gameMessage(event.peer, {.message = "action|logon_fail\n"}); // @note triggers action|quit on client.
        return;
    }
    auth::login_succeeded(event.peer->address);

    /* @note the client proves it's the same login when it reconnects, see action::tankIDName */
    const auth::ticket ticket = auth::issue_ticket(pPeer->user_id);
    pPeer->authenticated = false; // @note this connection is about to close. the next one has to show the ticket.

    send_varlist(event.peer, {
        "OnSendToServer",  
        (signed)gServer_data.port, 
        ticket.token, 
        pPeer->user_id, 
        std::format("{}|0|{}", gServer_data.server, ticket.uuid),
        1, 
        pPeer->growid.c_str()
    }); // @note PACKET_DISCONNECT from client.
}