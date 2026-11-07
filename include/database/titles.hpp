#pragma once

#include <map>

/*
 * Titles, like the real game's Titles menu (wrench yourself > Titles).
 *
 * A title can change how a player's name looks in four ways, each the way the real client draws it:
 * - prefix / suffix: text before or after the name ("Dr." + name, name + " of Legend").
 *   A name that ends in " of Legend``" also makes the client show the Legend flag.
 * - color: a Growtopia colour code for the whole name (Dr. is red, Legend is gold)
 * - badge: one of the words OnCountryState knows (maxLevel, doctor, donor, master, ccBadge,
 *   ttBadge, euphoriaTitle). Some of them replace the country flag with their own icon.
 * - texture + tex_x/tex_y: an icon next to the name, sent as the "title icon" JSON in
 *   OnSpawn (titleIcon) and OnNameChanged (2nd argument). Only one icon shows at a time.
 *
 * Titles live in this server's `titles` table so the website's Title Manager (web/src/titles.js)
 * can edit the built-in ones and make new ones. Which player has which title is in `peer_titles`.
 * A title is had by everyone, by everyone from a level up, by a role, or only when given
 * (website, /granttitle).
 */
struct title_def
{
    int id{};
    std::string name{};
    std::string description{};
    std::string prefix{};
    std::string suffix{};
    std::string color{};
    std::string badge{};
    std::string texture{};
    int tex_x{}, tex_y{};
    std::string grant{"manual"}; // @note manual, everyone, level or role
    int grant_value{};
    bool enabled{true};
    int sort{};
};

namespace titles
{
    /* @note creates the tables with the real game's titles, then reads them */
    extern void init();
    /* @note re-reads the titles (the website changed them), then refreshes everyone online */
    extern void reload();
    /* @note reads this player's rows from peer_titles. call before they enter a world */
    extern void load_peer(::peer &peer);
    /* @note the website gave or took a title: re-read this player's rows and show the change */
    extern void sync_peer(ENetPeer *peer);

    extern const title_def *find(const std::string &name_or_id);
    extern bool owns(const ::peer &peer, const title_def &title);
    /* @return the titles they have that are switched on, in order */
    extern std::vector<const title_def*> shown(const ::peer &peer);

    /* @return the name colour code from their titles, or "" */
    extern std::string color(const ::peer &peer);
    extern std::string prefix(const ::peer &peer);
    extern std::string suffix(const ::peer &peer);
    /* @return "us|maxLevel|doctor": what OnCountryState and OnSpawn's country take */
    extern std::string country_state(const ::peer &peer);
    /* @return the title icon JSON, or "" for no icon */
    extern std::string icon(const ::peer &peer);

    /* @note sends this player's name, icon and badges to everyone in their world */
    extern void refresh(ENetPeer *peer);

    /* @note gives (on) or takes away a title, online or not. @return false if there's no such account */
    extern bool grant(const std::string &growid, const title_def &title, bool on);

    /* @note after they level up: tells them about any title they just earned */
    extern void leveled_up(ENetPeer *peer, int old_level, int new_level);

    /* @note the Titles menu (wrench > Title) and what it sends back */
    extern void show_menu(ENetEvent &event);
    extern void menu_return(ENetEvent &event, const ::hPipe &hPipe);
}

/* @note staff commands */
extern void granttitle(ENetEvent &event, const std::string_view text);
extern void removetitle(ENetEvent &event, const std::string_view text);
