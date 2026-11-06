#include "pch.hpp"
#include <algorithm>
#include <chrono>

#include "onVariant/ConsoleMessage.hpp"
#include "onVariant/SetClothing.hpp"
#include "tools/sound.hpp"

#include "transform.hpp"

namespace
{
    /* @note one wrench menu button. `image` is interface/large/gui_wrench_<image>.rttex (the picture already says "Transform") */
    struct transform_set
    {
        const char *image;
        std::array<u_short, 5> items; // @note 0 = unused
        int bit{-1}; // @note OnFlagMay2019 bit (sent as 256 | 1 << bit), -1 = not known yet
    };

    /*
    * @note which worn item gets which Transform button. matched by item name/info ("Wrench yourself and choose Transform")
    *       and the button pictures in the game folder.
    */
    constexpr transform_set sets[] {
        { "space_animals_transform", { 10952/*Space Badger*/, 10954/*Space Mouse*/, 10956/*Space Dog*/, 10958/*Space Pig*/, 10960/*Space Rabbit*/ }, 18 },
        { "space_cat",               { 15992/*Space Cat*/ }, 18 },
        { "st_ani",                  { 12866/*Street Dog*/, 12868/*Street Rabbit*/ }, 25 },
        { "st_transform",            { 12014/*Dal's Phasers*/, 12016/*Gwyn's Heirloom*/, 12018/*Rok's Fists*/, 12020/*Jankom Pog's Multi-Mitt*/ }, 21 },
        { "rpan",                    { 12872/*Red Panda Spirit*/, 12874/*Royal Red Panda Spirit*/ }, 26 },
        { "mask_dragon_transform",   { 11506/*Mask of The Dragon*/, 11508/*Royal Mask of The Dragon*/ }, 19 },
        { "msf_transform",           { 10426/*Mystic Snow Fox*/ }, 11 },
        { "pps_transform",           { 10618/*Perilous Pirate Ship*/ }, 12 }, // @note karim found bit 12 with /transformscan
        { "ggg_transform",           { 7384/*Go-Go-Growformer!*/ } },
        { "dsf_transform",           { 9136/*Dueling Star Fighter - Rebel Raider*/, 9138/*Dueling Star Fighter - Imperial Enforcer*/ } },
        { "tf",                      { 14068/*Beastformer!*/, 14070/*Royal Beastformer!*/ } },
        { "maverick",                { 15840/*Maverick Armor*/, 15842/*Royal Maverick Armor*/ } },
        { "swat_truck",              { 15286/*SWAT Truck*/ } },
        { "bone_bike",               { 15694/*Bone Bike*/ } },
        { "hugo",                    { 14710/*Misthios' Hugo*/ } },
        { "uzxi",                    { 15596/*uzxi's Cultist Locket*/ } },
        { "u_transform",             { 11560/*Ezio*/ }, 20 },
        { "acv2",                    { 12646/*Altair*/, 12648/*Eivor*/, 12650/*Bayek*/ }, 23 },
        { "a15",                     { 13892/*Basim*/, 13894/*Connor*/, 13896/*Kassandra*/ }, 23 },
        { "acweek25_transform",      { 15290/*Yasuke*/ }, 23 },
        { "u2_transform",            { 13196/*Rabbid Buddy*/, 13220/*Aurora and Igniculus*/ } },
        { "u26",                     { 15674/*Battalion Chestplate*/, 15676/*Royal Battalion Chestplate*/ } }, // @note my best match for this button
    };

    /* @note "use on yourself" items that flip a worn item's form, and the item they need */
    struct transform_key { short key; u_short needs; };
    constexpr transform_key keys[] {
        { 7480/*Growformer PowerKey*/, 7384/*Go-Go-Growformer!*/ },
        { 10456/*Mystic Transform Crystal*/, 10426/*Mystic Snow Fox*/ },
        { 10620/*Magical Captain's Compass*/, 10618/*Perilous Pirate Ship*/ },
    };

    /* @note Diamond Horn -> Diamond Horns -> Diamond Devil Horns -> Diamond Horn, each time it's taken off */
    constexpr u_short diamond_horns[] { 4746, 4748, 4750 };

    constexpr u_short GROWFORMER = 7384;

    const transform_set *set_of(u_short id)
    {
        if (id == 0) return nullptr;
        for (const transform_set &set : sets)
            if (std::ranges::find(set.items, id) != set.items.end()) return &set;
        return nullptr;
    }

    bool wears(const ::peer &peer, u_short id)
    {
        return id != 0 && std::ranges::find(peer.clothing, static_cast<float>(id)) != peer.clothing.end();
    }

    /* @note the Diamond Dust locket transforms whoever wears it (OnFlagMay2019 DUST, 1 << 10) */
    constexpr u_short DIAMOND_DUST = 10412;
    constexpr int DUST_BIT = 10;

    /* @return the OnFlagMay2019 value for this player: 256 is "normal", plus one bit for the transform (space animals 1 << 18, dragon mask 1 << 19...) */
    u_int flag_value(const ::peer &peer)
    {
        if (peer.flag_test >= 0) return 256u | (1u << peer.flag_test); // @note /transformscan, /transformflag
        if (const transform_set *set = set_of(peer.transformed); set != nullptr && set->bit >= 0) return 256u | (1u << set->bit);
        if (wears(peer, DIAMOND_DUST)) return 256u | (1u << DUST_BIT);
        return 256u;
    }

    /* @brief what `p` needs to draw this player's transform */
    void send_look(ENetPeer *p, const ::peer &peer)
    {
        send_varlist(p, { "OnFlagMay2019", flag_value(peer) }, peer.netid);
        if (peer.transformed != 0) send_varlist(p, { "OnSetItemTransform", static_cast<int>(peer.transformed) }, peer.netid);
        else send_varlist(p, { "OnClearItemTransforms" }, peer.netid);
    }

    /* @brief tell everyone in the world (the player too) how this player looks now */
    void broadcast(ENetPeer *peer)
    {
        ::peer *pPeer = static_cast<::peer*>(peer->data);
        if (pPeer->netid == 0) return;
        peers(pPeer->recent_worlds.back(), PeerSearch::WORLD, [pPeer](ENetPeer *p) { send_look(p, *pPeer); });
    }

    /* @brief flip `id` between its normal and transformed look */
    void toggle(ENetPeer *peer, u_short id)
    {
        ::peer *pPeer = static_cast<::peer*>(peer->data);
        pPeer->transformed = (pPeer->transformed == id) ? 0 : id;
        broadcast(peer);
        if (id == GROWFORMER) on::SetClothing(peer, false, true); // @note robot form / back to wheels
        if (pPeer->netid != 0) play_in_world(pPeer->recent_worlds.back(), pPeer->netid, "audio/transform.wav");
    }
}

std::string transform_buttons(const ::peer &peer)
{
    std::string buttons{};
    std::vector<const transform_set*> shown{};
    for (const float cloth : peer.clothing)
    {
        const u_short id = static_cast<u_short>(cloth);
        const transform_set *set = set_of(id);
        if (set == nullptr || std::ranges::find(shown, set) != shown.end()) continue;
        shown.push_back(set);
        buttons.append(std::format(
            "add_custom_button|transform_{}|image:interface/large/gui_wrench_{}.rttex;image_size:400,260;width:0.19;|\n",
            id, set->image));
    }
    return buttons;
}

bool transform_button_clicked(ENetEvent &event, const std::string &button)
{
    if (!button.starts_with("transform_")) return false;
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    const u_short id = static_cast<u_short>(atoi(button.c_str() + 10));
    if (set_of(id) == nullptr) return true;
    if (!wears(*pPeer, id))
    {
        on::ConsoleMessage(event.peer, std::format("You need to wear `2{}`` to transform!", id_to_item(id).raw_name));
        return true;
    }
    toggle(event.peer, id);
    return true;
}

bool transform_consumable(ENetEvent &event, short id)
{
    auto key = std::ranges::find(keys, id, &transform_key::key);
    if (key == std::end(keys)) return false;
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    if (!wears(*pPeer, key->needs))
    {
        send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, std::format("You need to wear `2{}`` to use this!", id_to_item(key->needs).raw_name), 0u, 1u });
        return true;
    }
    modify_item_inventory(event, ::slot(id, -1));
    toggle(event.peer, key->needs);
    return true;
}

void transform_clothes_changed(ENetEvent &event, u_short was_wearing, u_short now_wearing)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    if (pPeer->transformed != 0 && !wears(*pPeer, pPeer->transformed))
    {
        pPeer->transformed = 0; // @note took the item off: back to normal
        broadcast(event.peer);
        on::SetClothing(event.peer, false, true); // @note e.g. the Growformer robot is gone too
    }
    else if (was_wearing == DIAMOND_DUST || now_wearing == DIAMOND_DUST) broadcast(event.peer);

    /* @note "This amazing horn transforms each time you unequip it. Try all 3 flavors!" */
    if (was_wearing != 0 && was_wearing != now_wearing)
    {
        auto horn = std::ranges::find(diamond_horns, was_wearing);
        if (horn != std::end(diamond_horns) && pPeer->have_slot(::slot(was_wearing, 0)) > 0)
        {
            const u_short next = diamond_horns[(std::distance(std::begin(diamond_horns), horn) + 1) % std::size(diamond_horns)];
            if (pPeer->can_hold(next, 1))
            {
                modify_item_inventory(event, ::slot(was_wearing, -1));
                modify_item_inventory(event, ::slot(next, 1));
                on::ConsoleMessage(event.peer, std::format("Your `2{}`` transformed into `2{}``!", id_to_item(was_wearing).raw_name, id_to_item(next).raw_name));
            }
        }
    }
}

void transform_entered(ENetEvent &event)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (pPeer->transformed != 0 && !wears(*pPeer, pPeer->transformed)) pPeer->transformed = 0;

    peers(pPeer->recent_worlds.back(), PeerSearch::WORLD, [&event, pPeer](ENetPeer *p)
    {
        const ::peer *pOthers = static_cast<::peer*>(p->data);
        if (p != event.peer) send_look(event.peer, *pOthers);
        send_look(p, *pPeer);
    });
}

void transform_flag_command(ENetEvent &event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const std::string arg = (text.find(' ') != std::string_view::npos) ? std::string(text.substr(text.find(' ') + 1)) : "";
    pPeer->flag_scan_next = 0;
    if (arg.empty() || arg == "off")
    {
        pPeer->flag_test = -1;
        on::ConsoleMessage(event.peer, "Transform flag test off. Usage: /transformflag <0-31>");
    }
    else
    {
        pPeer->flag_test = std::clamp(atoi(arg.c_str()), 0, 31);
        on::ConsoleMessage(event.peer, std::format("OnFlagMay2019 = 256 | 1 << {} ({})", pPeer->flag_test, flag_value(*pPeer)));
    }
    broadcast(event.peer);
}

void transform_scan_command(ENetEvent &event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (pPeer->flag_scan_next != 0 || text.find("stop") != std::string_view::npos)
    {
        pPeer->flag_scan_next = 0;
        pPeer->flag_test = -1;
        broadcast(event.peer);
        return on::ConsoleMessage(event.peer, "Transform scan stopped.");
    }
    pPeer->flag_test = -1;
    pPeer->flag_scan_next = 1; // @note transform_tick starts at bit 0
    on::ConsoleMessage(event.peer, "Transform scan: every 3 seconds the next flag bit is tried. Watch your player and note the bit when you transform. /transformscan again to stop.");
}

void transform_tick()
{
    static long long last{};
    const long long now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (now - last < 250) return;
    last = now;

    peers("", PeerSearch::EVERYONE, [now](ENetPeer *peer)
    {
        ::peer *pPeer = static_cast<::peer*>(peer->data);
        if (pPeer == nullptr || pPeer->flag_scan_next == 0 || now < pPeer->flag_scan_next) return;
        int bit = pPeer->flag_test + 1;
        if (bit == 8) ++bit; // @note 256 is always on
        if (bit > 31)
        {
            pPeer->flag_scan_next = 0;
            pPeer->flag_test = -1;
            broadcast(peer);
            on::ConsoleMessage(peer, "Transform scan finished (bits 0-31).");
            return;
        }
        pPeer->flag_test = bit;
        pPeer->flag_scan_next = now + 3000;
        broadcast(peer);
        send_varlist(peer, { "OnTalkBubble", pPeer->netid, std::format("`wflag bit `2{}``", bit), 0u, 1u });
        on::ConsoleMessage(peer, std::format("Trying flag bit {}", bit));
    });
}
