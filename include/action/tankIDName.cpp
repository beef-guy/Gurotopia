#include "pch.hpp"

#include "automate/holiday.hpp"
#include "tools/auth.hpp"
#include "tools/terminal.hpp"
#include "database/platform.hpp"
#include "commands/moderation.hpp"
#include "https/server_data.hpp"
#include "action/event_buttons.hpp"

#include <cctype>
#include <ctime>

#include "tankIDName.hpp"

/* GameLogicComponent::OnInitialLogonAccepted(VariantList*) */

void action::tankIDName(ENetEvent& event, const std::string& header)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (platform::gateway) // @note players only log in at the gateway, they play on the server it sends them to
    {
        send_gameMessage(event.peer, {.message = "action|logon_fail\n"});
        return;
    }

    std::vector<std::string> pipes = readch(header, '|');
    if (pipes.empty() || pipes.size() < 10ull) {
        enet_peer_disconnect_later(event.peer, 0);
        return;
    }

    /* @todo instead of setting these values we should check if there valid from SQL.*/
    std::string protocol{}, uuid_token{};
    int token = 0;
    for (std::size_t i = 0; i + 1 < pipes.size(); ++i)
    {
        if      (pipes[i] == "tankIDName") pPeer->growid = pipes[i+1];
        else if (pipes[i] == "tankIDPass") pPeer->password = pipes[i+1];
        else if (pipes[i] == "protocol")   protocol = pipes[i+1];
        else if (pipes[i] == "country")    pPeer->country = pipes[i+1];
        else if (pipes[i] == "user")       pPeer->user_id = std::atoi(pipes[i+1].c_str());
        else if (pipes[i] == "token")      token = std::atoi(pipes[i+1].c_str());
        else if (pipes[i] == "UUIDToken")  uuid_token = pipes[i+1];
        else if (pipes[i] == "game_version") 
        {
            try { 
                pPeer->game_version = std::stof(pipes[i+1]); 
            } 
            catch (const std::logic_error &le) {} // @note std::invalid_argument std::out_of_range
        }
    }
    if (atoi(protocol.c_str()) == 0) return; // @todo <226

    bool loaded = false;
    if (pPeer->user_id > 0)
    {
        /* @note only someone who just typed the right password gets a ticket for this account */
        if (!auth::login_blocked(event.peer->address) && auth::redeem_ticket(pPeer->user_id, token, uuid_token))
        {
            loaded = pPeer->load_by_uid(pPeer->user_id);
        }
        else {
            auth::login_failed(event.peer->address);
            print(std::format("refused login for user {}: missing or wrong login ticket", pPeer->user_id));
        }
    }
    else if (!pPeer->growid.empty() && !auth::login_blocked(event.peer->address)) {
        loaded = pPeer->load(pPeer->growid, pPeer->password);
        if (!loaded) auth::login_failed(event.peer->address);
        else auth::login_succeeded(event.peer->address);
    }
    pPeer->password.clear(); // @note don't keep the password around once it's checked
    if (!loaded)
    {
        pPeer->authenticated = false;
        send_gameMessage(event.peer, {.message = "action|logon_fail\n"}); // @note triggers action|quit on client.
        return;
    }

    /* @note banned accounts and IPs can't come in */
    {
        std::string reason{};
        long long until = pPeer->banned_until;
        const long long now = std::time(nullptr);
        bool banned = (until < 0 || until > now);
        if (banned) reason = pPeer->ban_reason;
        else if (pPeer->role < MODERATOR) banned = moderation::ip_banned(event.peer->address, reason, until); // @note staff can't be locked out by an IP ban
        if (banned)
        {
            print(std::format("refused login for {}: banned", pPeer->growid));
            pPeer->authenticated = false; // @note so nothing is saved over the account on the way out
            send_gameMessage(event.peer, {.message = std::format(
                "action|log\nmsg|`4Sorry, this account or device is banned{}.``{}",
                (until < 0) ? "" : " for " + moderation::duration(until - now), reason.empty() ? "" : " Reason: " + reason)});
            send_gameMessage(event.peer, {.message = "action|logon_fail\n"});
            enet_peer_disconnect_later(event.peer, 0);
            return;
        }
    }
    /* @note the server's owner (--owner) always gets developer rights */
    if (!gServer_data.owner.empty() && pPeer->role_id != DEVELOPER)
    {
        std::string a = gServer_data.owner, b = pPeer->growid;
        for (char &c : a) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (char &c : b) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (a == b)
        {
            pPeer->role_id = DEVELOPER;
            pPeer->role = DEVELOPER;
            pPeer->mysql_update("role", static_cast<signed>(DEVELOPER));
        }
    }

    send_varlist(event.peer, { "OnOverrideGDPRFromServer", 18, 1, 0, 1 });

    /* v5.61 */
    send_varlist(event.peer, {
        "OnSuperMainStartAcceptLogonHrdxs47254722215a",
        items_hash, // @note items.dat: a client whose copy is different asks for this server's (refresh_item_data)
        "ubistatic-a.akamaihd.net",
        "0098/01020126/cache/",
        "cc.cz.madkite.freedom org.aqua.gg idv.aqua.bulldog com.cih.gamecih2 com.cih.gamecih com.cih.game_cih cn.maocai.gamekiller com.gmd.speedtime org.dax.attack com.x0.strai.frep com.x0.strai.free org.cheatengine.cegui org.sbtools.gamehack com.skgames.traffikrider org.sbtoods.gamehaca com.skype.ralder org.cheatengine.cegui.xx.multi1458919170111 com.prohiro.macro me.autotouch.autotouch com.cygery.repetitouch.free com.cygery.repetitouch.pro com.proziro.zacro com.slash.gamebuster",
        std::format(
            "proto={}|choosemusic=audio/mp3/about_theme.mp3|active_holiday={}|wing_week_day=0|ubi_week_day=0|server_tick=0|game_theme={}|clash_active=0|drop_lavacheck_faster=1|isPayingUser=1|usingStoreNavigation=1|enableInventoryTab=1|bigBackpack=1|seed_diary_hash=3856714732", 
            protocol, (u_char)active_holiday, game_theme_string()
        ) + "|m_clientBits=|eventButtons=" + event_buttons_json(*pPeer)
    });
}