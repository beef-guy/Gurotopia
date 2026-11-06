#include "pch.hpp"

#include "onVariant/ConsoleMessage.hpp"
#include "onVariant/SetClothing.hpp"
#include "tools/sound.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_map>

#include "gauntlet.hpp"

namespace
{
    /* @note which stone takes which gauntlet to which */
    struct stone_step { short stone, from, to; const char *element; };
    constexpr stone_step steps[] {
        { EARTH_STONE, RAW_GAUNTLET,      GAUNTLET_TIER_I,    "Earth" },
        { WIND_STONE,  GAUNTLET_TIER_I,   GAUNTLET_TIER_II,   "Wind"  },
        { FIRE_STONE,  GAUNTLET_TIER_II,  GAUNTLET_TIER_III,  "Fire"  },
        { DEATH_STONE, GAUNTLET_TIER_III, GAUNTLET_TIER_IV,   "Death" },
        { WATER_STONE, GAUNTLET_TIER_IV,  GAUNTLET_TIER_V,    "Water" },
        { LIFE_STONE,  GAUNTLET_TIER_V,   GAUNTLET_EXQUISITE, "Life"  },
    };

    /* @return the chi the worn gauntlet can move (chi:: bits). 0 if not wearing a gauntlet that moves blocks */
    u_char gauntlet_chi(short hand)
    {
        switch (hand)
        {
            case GAUNTLET_TIER_I:    return CHI_EARTH;
            case GAUNTLET_TIER_II:   return CHI_EARTH | CHI_WIND;
            case GAUNTLET_TIER_III:
            case GAUNTLET_TIER_IV:   return CHI_EARTH | CHI_WIND | CHI_FIRE;
            case GAUNTLET_TIER_V:
            case GAUNTLET_EXQUISITE: return CHI_EARTH | CHI_WIND | CHI_FIRE | CHI_WATER;
        }
        return 0;
    }

    /* @return true for blocks the gauntlet can move. locks, the Main Door, bedrock and MAGPLANTs (their remotes point at them) stay put */
    bool movable(const ::item &item)
    {
        if (item.id == 0 || item.id == 8/*Bedrock*/ || item.id == 3760/*Data Bedrock*/ || item.id == 7372/*Data Bedrock*/) return false;
        switch (item.type)
        {
            case type::LOCK: case type::MAIN_DOOR: case type::MAGPLANT: case type::STRONG: case type::BACKGROUND:
                return false;
        }
        return true;
    }

    /* @brief everything the world keeps by tile position (door ids, vending stock, heart monitor owner...) moves along */
    void swap_tile_data(::world &world, const ::pos &a, const ::pos &b)
    {
        auto swap_in = [&a, &b](auto &list)
        {
            for (auto &entry : list)
            {
                if (entry.pos == a) entry.pos = b;
                else if (entry.pos == b) entry.pos = a;
            }
        };
        swap_in(world.doors); swap_in(world.random_blocks); swap_in(world.signs); swap_in(world.trees);
        swap_in(world.vendings); swap_in(world.storages); swap_in(world.displays); swap_in(world.providers);
        swap_in(world.mailboxes); swap_in(world.donation_boxes); swap_in(world.shelves); swap_in(world.heart_monitors);
        swap_in(world.mannequins); swap_in(world.weather_settings); swap_in(world.fish_tanks); swap_in(world.fish_mounts);
        swap_in(world.lobster_traps); swap_in(world.auto_machines); swap_in(world.timed_machines);
        for (auto &[pos, life] : world.fossil_life)
        {
            if (pos == a) pos = b;
            else if (pos == b) pos = a;
        }
    }

    /*
    * @brief SELECT_TILE_INDEX (0x26): the game draws the green "swap" icon on `main` and the dashed outline on every tile in `others`.
    * @note tile indexes (y * 100 + x). -1 clears it. it fades by itself after 30 seconds. only the holder sees it.
    */
    void select_tiles(ENetPeer *peer, int main, const std::vector<int> &others = {})
    {
        ::blob blob = compress_state(::gamePacket{
            .type = 0x26,
            .netid = main,
            .state = (int)gamePacketState::EXTENDED,
            .size = static_cast<u_int>(others.empty() ? 0 : 4 + 4 * others.size())
        });
        if (!others.empty())
        {
            blob.i32(static_cast<int>(others.size()));
            for (int index : others) blob.i32(index);
        }
        send_data(*peer, blob);
    }

    /* @brief ITEM_EFFECT (0x13): a small copy of the block flies from one tile to the other, everyone in the world sees it */
    void fly_block(const ::world &world, short id, const ::pos &from, const ::pos &to)
    {
        if (id == 0) return;
        ::blob blob = compress_state(::gamePacket{
            .type  = 0x13,
            .uid   = -1,
            .delay = 200,
            .pos   = ::pos{to.x * 32.0f + 16.0f, to.y * 32.0f + 16.0f},
            .speed = ::pos{from.x * 32.0f + 16.0f, from.y * 32.0f + 16.0f},
            .punch = ::pos{static_cast<int>(id), static_cast<int>(id)}
        });
        peers(world.name, PeerSearch::WORLD, [&blob](ENetPeer *peer) { send_data(*peer, blob); });
    }

    struct picked { std::string world; ::pos pos; };
    std::unordered_map<int, picked> holding; // @note user id -> the block they picked up (until they swap it or leave)

    void bubble(ENetEvent &event, const std::string &text)
    {
        ::peer *pPeer = static_cast<::peer*>(event.peer->data);
        send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, text, 0u, 1u });
    }
}

u_char block_chi(const ::item &item)
{
    std::string name{};
    for (char c : item.raw_name) name += static_cast<char>(std::tolower(static_cast<u_char>(c)));
    /* @note whole words only, so "season" isn't the sea */
    std::vector<std::string> words{};
    std::string word{};
    for (char c : name)
    {
        if (std::isalpha(static_cast<u_char>(c))) word += c;
        else if (!word.empty()) words.push_back(word), word.clear();
    }
    if (!word.empty()) words.push_back(word);

    auto any = [&words](std::initializer_list<std::string_view> list)
    {
        return std::ranges::any_of(words, [&list](const std::string &w) {
            return std::ranges::any_of(list, [&w](std::string_view k) { return w == k || (k.size() >= 4 && w.starts_with(k)); });
        });
    };
    if (any({"lava", "fire", "flame", "flaming", "magma", "volcanic", "volcano", "ember", "torch", "candle", "inferno", "heat", "burning",
             "blaze", "blazing", "coal", "furnace", "fireplace", "hell", "hellfire", "brimstone", "phoenix", "chili", "sun", "solar",
             "obsidian", "sulfur", "ash", "hot", "lantern", "bonfire", "campfire", "spicy", "pepper", "dragon", "infernal"}))
        return CHI_FIRE;
    if (any({"water", "aqua", "aquatic", "ocean", "sea", "wave", "river", "ice", "icy", "snow", "frost", "frozen", "rain", "bubble",
             "coral", "pool", "bath", "puddle", "sink", "fountain", "fish", "shell", "barrel", "glacier", "slime", "jelly", "acid",
             "oil", "tide", "reef", "marine", "underwater", "seaweed", "kelp", "pipe", "aquarium", "tank", "waterfall", "lake", "pond"}))
        return CHI_WATER;
    if (any({"glass", "cloud", "air", "wind", "sky", "window", "pane", "feather", "balloon", "kite", "fan", "smoke", "fog", "mist",
             "storm", "tornado", "star", "lightning", "thunder", "crystal", "breeze", "heaven", "heavenly", "angel", "angelic",
             "space", "galaxy", "moon", "nebula", "aurora", "rainbow", "light"}))
        return CHI_WIND;
    return CHI_EARTH; // @note dirt, rock, wood, brick... most blocks are Earth
}

void elemental_stone_use(ENetEvent &event, short stone)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const auto step = std::ranges::find(steps, stone, &stone_step::stone);
    if (step == std::ranges::end(steps)) return;

    const short hand = static_cast<short>(pPeer->clothing[clothing::HAND]);
    if (hand != step->from)
    {
        if (hand < RAW_GAUNTLET || hand > GAUNTLET_EXQUISITE)
            throw std::runtime_error("Raw elemental power surges within this stone! You need to wear a `2Gauntlet of Elements`` to hold it.");
        throw std::runtime_error(std::format("The {} Stone can only be bound to a `2{}``!", step->element, id_to_item(step->from).raw_name));
    }
    if (pPeer->have_slot(::slot(SOUL_STONE, 0)) <= 0)
        throw std::runtime_error("Binding a stone requires the sacrifice of a soul in similar form. You need a `2Soul Stone``!");
    if (pPeer->have_slot(::slot(step->to, 0)) >= 200) throw std::runtime_error("You can't carry any more of those!");

    modify_item_inventory(event, ::slot(stone, -1));
    modify_item_inventory(event, ::slot(SOUL_STONE, -1));
    modify_item_inventory(event, ::slot(step->from, -1));
    modify_item_inventory(event, ::slot(step->to, 1));

    pPeer->clothing[clothing::HAND] = step->to;
    pPeer->update_effects();
    send_varlist(event.peer, { "OnEquipNewItem", static_cast<int>(step->to) }, pPeer->netid);
    on::SetClothing(event.peer, true);
    holding.erase(pPeer->user_id);

    const std::string text = std::format("The {} Stone is bound to the gauntlet! You now have a `2{}``!", step->element, id_to_item(step->to).raw_name);
    bubble(event, text);
    on::ConsoleMessage(event.peer, text);
    send_varlist(event.peer, { "OnParticleEffect", 46u, CL_Vec2f{pPeer->pos.x + 16.0f, pPeer->pos.y + 16.0f}, 0.0f, 0.0f });
    play_in_world(pPeer->recent_worlds.back(), pPeer->netid, "audio/gauntlet_spawn.wav");
}

bool gauntlet_punch(ENetEvent &event, ::world &world, const ::gamePacket &gamePacket)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const u_char can_move = gauntlet_chi(static_cast<short>(pPeer->clothing[clothing::HAND]));
    if (can_move == 0) return false;
    if (!pPeer->owns(world.owner) && pPeer->role < MODERATOR) return false; // @note only the world's owner (not co-owners), like real Growtopia

    ::block &target = world.blocks[cord(gamePacket.punch.x, gamePacket.punch.y)];
    auto allowed = [can_move](short fg) { const ::item &item = id_to_item(fg); return movable(item) && (block_chi(item) & can_move); };

    auto held = holding.find(pPeer->user_id);
    if (held != holding.end() && (held->second.world != world.name || !valid_tile(world, held->second.pos) ||
        !allowed(world.blocks[cord(held->second.pos.x_int(), held->second.pos.y_int())].fg)))
    {
        holding.erase(held); // @note left the world, or the block is gone
        held = holding.end();
        select_tiles(event.peer, -1);
    }

    if (held == holding.end()) // @note first punch: pick the block up
    {
        if (target.fg == 0) return false; // @note nothing to pick up, punch the background as normal
        if (!allowed(target.fg))
        {
            const ::item &item = id_to_item(target.fg);
            if (!movable(item)) return false; // @note locks, bedrock... punch as normal
            static constexpr const char *names[] { "", "Earth", "Wind", "", "Fire", "", "", "", "Water" };
            bubble(event, std::format("Your gauntlet isn't strong enough to move blocks with {} Chi yet!", names[block_chi(item)]));
            return true;
        }
        holding[pPeer->user_id] = picked{ world.name, gamePacket.punch };

        /* @note every block it could swap with, within reach, gets the dashed outline */
        std::vector<int> others{};
        const ::pos standing = pPeer->pos.by_32(true);
        const int reach = 6 + std::max(pPeer->punch_range, pPeer->build_range);
        for (int y = std::max(0, standing.y_int() - reach); y <= standing.y_int() + reach; ++y)
            for (int x = std::max(0, standing.x_int() - reach); x <= standing.x_int() + reach; ++x)
            {
                if (!valid_tile(world, ::pos{x, y}) || (x == gamePacket.punch.x && y == gamePacket.punch.y)) continue;
                if (allowed(world.blocks[cord(x, y)].fg)) others.push_back(cord(x, y));
            }
        select_tiles(event.peer, cord(gamePacket.punch.x, gamePacket.punch.y), others);
        play_sfx(event.peer, "audio/punch_locked.wav");
        return true;
    }

    const ::pos from = held->second.pos;
    holding.erase(held);
    select_tiles(event.peer, -1); // @note the green marks go away
    if (from == gamePacket.punch) // @note punched the same block again: put it down
    {
        return true;
    }
    if (target.fg != 0 && !allowed(target.fg))
    {
        bubble(event, "That block can't be moved!");
        return true;
    }

    ::block &source = world.blocks[cord(from.x_int(), from.y_int())];
    std::swap(source.fg, target.fg);
    source.hits[0] = target.hits[0] = 0;
    constexpr u_char keep2 = S_PARENT; // @note area lock membership stays with the tile, the block's own flags move with it
    const u_char s2 = source.state[2], t2 = target.state[2];
    source.state[2] = (s2 & keep2) | (t2 & ~keep2);
    target.state[2] = (t2 & keep2) | (s2 & ~keep2);
    const u_char s3 = source.state[3], t3 = target.state[3]; // @note paint and glue move with the block, water and fire stay
    constexpr u_char move3 = S_VANISH | S_GLUE;
    source.state[3] = (s3 & ~move3) | (t3 & move3);
    target.state[3] = (t3 & ~move3) | (s3 & move3);

    swap_tile_data(world, from, gamePacket.punch);
    fly_block(world, target.fg, from, gamePacket.punch);
    fly_block(world, source.fg, gamePacket.punch, from);
    send_tile_update(event, ::gamePacket{.id = source.fg, .punch = from}, source, world);
    send_tile_update(event, ::gamePacket{.id = target.fg, .punch = gamePacket.punch}, target, world);
    play_in_world(world.name, pPeer->netid, "audio/punch_locked.wav");
    return true;
}
