#pragma once

/* @note staff commands. who may use each one is set in __command.cpp */

extern void kick(ENetEvent& event, const std::string_view text);
extern void ban(ENetEvent& event, const std::string_view text);
extern void unban(ENetEvent& event, const std::string_view text);
extern void mute(ENetEvent& event, const std::string_view text);
extern void unmute(ENetEvent& event, const std::string_view text);
extern void pull(ENetEvent& event, const std::string_view text);
extern void warpto(ENetEvent& event, const std::string_view text);
extern void broadcast(ENetEvent& event, const std::string_view text);
extern void give(ENetEvent& event, const std::string_view text);
extern void setrole(ENetEvent& event, const std::string_view text);
extern void givegems(ENetEvent& event, const std::string_view text);
extern void trashall(ENetEvent& event, const std::string_view text);
extern void playermode(ENetEvent& event, const std::string_view text);
extern void mail(ENetEvent& event, const std::string_view text);
extern void maillayout(ENetEvent& event, const std::string_view text);
extern void mailrewards(ENetEvent& event, const std::string_view text);

namespace moderation
{
    /* @return the connected, logged in peer whose growid matches (any case), or nullptr */
    extern ENetPeer *find_online(const std::string &growid);

    /* @return "1.2.3.4" */
    extern std::string ip_string(const ENetAddress &address);

    /* @return true and fill reason/until when this IP is banned right now */
    extern bool ip_banned(const ENetAddress &address, std::string &reason, long long &until);

    /* @note writes one row to the mod_log table, so the website can show who did what */
    extern void log(const ::peer &actor, const std::string &action, const std::string &target, const std::string &detail);

    /* @return "3 hours", "12 minutes", ... for a number of seconds */
    extern std::string duration(long long seconds);
}
