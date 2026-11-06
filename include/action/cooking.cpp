#include "pch.hpp"

#include "onVariant/ConsoleMessage.hpp"
#include "tools/create_dialog.hpp"
#include "tools/random.hpp"
#include "tools/sound.hpp"

#include "cooking.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>

namespace
{
    constexpr short SALT   = 4568;
    constexpr short PEPPER = 4570;
    constexpr short SUGAR  = 4572;
    constexpr short WATER_BUCKET = 822;
    constexpr short FLOUR  = 4562;
    constexpr short DOUGH  = 956;
    constexpr short DISGUSTING_MESS = 4574; // @note "cooking a proper recipe except everything is burnt/undercooked"
    constexpr short BLAND_MUSH      = 4576; // @note "dropped after every incorrectly cooking food"
    constexpr short GINGERBREAD_MAN = 5414;
    constexpr short FAILED_BUTTER_LAMB = 10746;
    constexpr short MASTER_CHEFS_OVEN = 4620;
    constexpr short REPLICATOR = 6412;

    constexpr short LABORATORY = 912;
    constexpr short EZ_COOK_OVEN = 952;
    constexpr short MICROWAVE = 1472;
    constexpr short VEHICLE_REPLICATOR = 12148;
    constexpr short CAMPFIRE = 1048;
    constexpr short CHOCOLATE_BLOCK = 2564;
    constexpr short ICY_IGLOO = 7454;

    constexpr std::size_t OVEN_MAX_PARTS = 20; // @note my pick: how many ingredients one oven holds
    constexpr int REPLICATOR_CHANCE = 10;      // @note my pick: % chance the Replicator makes two ("a small chance")
    constexpr int SPICE_PENALTY = 5;           // @note my pick: % quality lost per spice more or less than the recipe wants

    ::peer *peer_of(ENetPeer *peer) { return static_cast<::peer*>(peer->data); }

    long long unix_ms()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    }

    void bubble(ENetEvent &event, const std::string &text)
    {
        send_varlist(event.peer, { "OnTalkBubble", peer_of(event.peer)->netid, text, 0u, 1u });
    }

    bool is_spice(short id) { return id == SALT || id == PEPPER || id == SUGAR; }

    /* ---------------- recipes ---------------- */

    struct combine_recipe
    {
        short result{};
        short count{};
        std::array<std::pair<short, int>, 3> needs{}; // @note sorted by item id
        std::string where{};
    };

    struct oven_recipe
    {
        short result{};
        std::vector<short> needs{}; // @note sorted item ids, spices left out
        int salt{}, pepper{}, sugar{};
        std::string kind{};
    };

    std::vector<std::string> split(const std::string &line, char by)
    {
        std::vector<std::string> out;
        std::stringstream ss(line);
        for (std::string part; std::getline(ss, part, by);) out.push_back(part);
        return out;
    }

    const std::vector<combine_recipe> &combine_recipes()
    {
        static std::vector<combine_recipe> list = []
        {
            std::vector<combine_recipe> out;
            std::ifstream file("resources/combiner.txt");
            for (std::string line; std::getline(file, line);)
            {
                if (line.empty() || line[0] == '#') continue;
                const std::vector<std::string> f = split(line, '|');
                if (f.size() < 9) continue;
                try
                {
                    combine_recipe r{ .result = static_cast<short>(std::stoi(f[0])), .count = static_cast<short>(std::stoi(f[1])), .where = f[8] };
                    for (int i = 0; i < 3; ++i) r.needs[i] = { static_cast<short>(std::stoi(f[2 + i * 2])), std::stoi(f[3 + i * 2]) };
                    std::ranges::sort(r.needs);
                    out.push_back(r);
                }
                catch (const std::exception &) { print(std::format("resources/combiner.txt: skipped a bad line: {}", line)); }
            }
            if (out.empty()) print("resources/combiner.txt is missing or empty, Chemical Combiners won't make anything");
            return out;
        }();
        return list;
    }

    const std::vector<oven_recipe> &oven_recipes()
    {
        static std::vector<oven_recipe> list = []
        {
            std::vector<oven_recipe> out;
            std::ifstream file("resources/oven.txt");
            for (std::string line; std::getline(file, line);)
            {
                if (line.empty() || line[0] == '#') continue;
                const std::vector<std::string> f = split(line, '|');
                if (f.size() < 6) continue;
                try
                {
                    oven_recipe r{ .result = static_cast<short>(std::stoi(f[0])), .salt = std::stoi(f[2]), .pepper = std::stoi(f[3]), .sugar = std::stoi(f[4]), .kind = f[5] };
                    for (const std::string &id : split(f[1], ',')) r.needs.push_back(static_cast<short>(std::stoi(id)));
                    std::ranges::sort(r.needs);
                    out.push_back(r);
                }
                catch (const std::exception &) { print(std::format("resources/oven.txt: skipped a bad line: {}", line)); }
            }
            if (out.empty()) print("resources/oven.txt is missing or empty, ovens will only make Bland Mush");
            return out;
        }();
        return list;
    }

    /* ---------------- Chemical Combiner ---------------- */

    /* @return the tile a dropped item lies on (drops are drawn 16 pixels wide, so go by their middle) */
    ::pos object_tile(const ::object &object)
    {
        return ::pos{ std::floor((object.pos.x + 8.0f) / 32.0f), std::floor((object.pos.y + 8.0f) / 32.0f) };
    }

    bool combiner_can_make(const ::world &world, const ::pos &pos, short combiner, const std::string &where)
    {
        const int x = pos.x_int(), y = pos.y_int();
        if (combiner == VEHICLE_REPLICATOR) return where == "vehicle";
        if (where == "vehicle") return false;
        if (where == "microwave")
            return combiner == MICROWAVE && y + 1 < 60 && world.blocks[cord(x, y + 1)].fg == CHOCOLATE_BLOCK;
        if (where == "lab_water")
            return combiner == LABORATORY && (world.blocks[cord(x, y)].state[3] & S_WATER);
        if (where == "ezcook_campfire")
            return combiner == EZ_COOK_OVEN && y > 0 && world.blocks[cord(x, y - 1)].fg == CAMPFIRE;
        if (where == "igloos")
        {
            int igloos = 0;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                {
                    if (dx == 0 && dy == 0) continue;
                    const ::pos p{ x + dx, y + dy };
                    if (valid_tile(world, p) && world.blocks[cord(x + dx, y + dy)].fg == ICY_IGLOO) ++igloos;
                }
            return igloos >= 7;
        }
        return true;
    }

    /* @return true if the drops on an open combiner made something */
    bool combine(ENetEvent &event, ::world &world, const ::pos &pos, const ::block &block)
    {
        std::vector<std::pair<short, int>> inside;
        for (const ::object &object : world.objects)
        {
            if (object_tile(object) != pos && object.pos.by_32(true) != pos) continue;
            auto have = std::ranges::find(inside, object.id, &std::pair<short, int>::first);
            if (have != inside.end()) have->second += object.count;
            else inside.emplace_back(object.id, object.count);
        }
        if (inside.size() != 3) return false;
        std::ranges::sort(inside);

        const combine_recipe *found = nullptr;
        for (const combine_recipe &r : combine_recipes())
            if (std::ranges::equal(r.needs, inside) && combiner_can_make(world, pos, block.fg, r.where)) { found = &r; break; }
        if (found == nullptr) return false;

        std::vector<u_int> used;
        for (const ::object &object : world.objects)
            if (object_tile(object) == pos || object.pos.by_32(true) == pos) used.push_back(object.uid);
        for (const u_int uid : used)
        {
            std::erase_if(world.objects, [uid](const ::object &o) { return o.uid == uid; });
            remove_object(event, static_cast<int>(uid));
        }
        add_object(event, ::slot(found->result, found->count), ::pos{ pos.x * 32.0f + 8.0f, pos.y * 32.0f + 8.0f }, world);

        const ::item &made = id_to_item(found->result);
        const std::string text = (found->count > 1) ?
            std::format("`wYou made {} {}!``", found->count, made.raw_name) :
            std::format("`wYou made a {}!``", made.raw_name);
        bubble(event, text);
        on::ConsoleMessage(event.peer, text);
        play_in_world(world.name, peer_of(event.peer)->netid, "audio/terraform.wav");
        world_tile_update(world, pos); // @note it stays open, whatever the punch looked like to the game
        world.save();
        return true;
    }

    /* ---------------- Cooking Oven ---------------- */

    ::oven *oven_at(::world &world, const ::pos &pos)
    {
        auto o = std::ranges::find(world.ovens, pos, &::oven::pos);
        return (o != world.ovens.end()) ? &*o : nullptr;
    }

    /* @brief bring every ingredient up to now, then reset the clock */
    void settle(::oven &oven)
    {
        oven.parts = oven_parts_now(oven);
        oven.since = unix_ms();
    }

    /* @brief how close one ingredient is to Perfect, 0-100. the game calls 95-104% Perfect */
    int ingredient_score(short id, int value)
    {
        const int cook = id_to_item(id).ingredient;
        if (cook == 0 || cook == -1) return -1; // @note doesn't cook (Flour, Honey, spices...)
        if (cook > 0)
        {
            const int percent = value * 100 / cook;
            return std::clamp(100 - std::abs(percent - 100), 0, 100);
        }
        const int percent = value * 100 / -cook; // @note raw ingredients (Lettuce, Lemon...): Perfect until 100%, then Overcooked and Burnt
        return (percent < 100) ? 100 : std::clamp(100 - (percent - 100), 0, 100);
    }

    void add_ingredient(ENetEvent &event, ::world &world, ::oven &oven, short id)
    {
        settle(oven);
        oven.parts.emplace_back(id, 0);

        /* @note "Water Bucket and Flour make Dough" (ingredients that don't cook combine right away) */
        auto water = std::ranges::find(oven.parts, WATER_BUCKET, &std::pair<short, int>::first);
        auto flour = std::ranges::find(oven.parts, FLOUR, &std::pair<short, int>::first);
        if (water != oven.parts.end() && flour != oven.parts.end())
        {
            std::erase_if(oven.parts, [&, w = false, f = false](const std::pair<short, int> &p) mutable {
                if (p.first == WATER_BUCKET && !w) return w = true;
                if (p.first == FLOUR && !f) return f = true;
                return false;
            });
            oven.parts.emplace_back(DOUGH, 0);
            bubble(event, "The `wWater Bucket`` and `wFlour`` turned into `wDough``!");
        }
        modify_item_inventory(event, ::slot(id, -1));
        world_tile_update(world, oven.pos);
        world.save();
    }

    void oven_heat_dialog(ENetEvent &event, const ::pos &pos, short id, short oven_id)
    {
        const ::item &ingredient = id_to_item(id);
        send_varlist(event.peer, { "OnDialogRequest", ::create_dialog()
            .set_default_color("`o")
            .add_label_with_icon("big", std::format("`w{}``", id_to_item(oven_id).raw_name), oven_id)
            .add_textbox(std::format("You're putting `w{}`` in the oven. How hot should it cook?", ingredient.raw_name))
            .add_smalltext("Higher heat cooks faster, but it's harder to get the timing right.")
            .embed_data("tilex", pos.x_int())
            .embed_data("tiley", pos.y_int())
            .embed_data("ingredient", id)
            .add_button("low", "Low Heat")
            .add_button("medium", "Medium Heat")
            .add_button("high", "High Heat")
            .end_dialog("oven_heat", "Cancel", "") });
    }

    /* @brief punch the oven: take out whatever it made */
    void cook(ENetEvent &event, ::world &world, ::oven &oven, short oven_id)
    {
        ::peer *pPeer = peer_of(event.peer);
        settle(oven);

        std::vector<short> needs;
        int salt = 0, pepper = 0, sugar = 0, score_sum = 0, scored = 0;
        for (const auto &[id, value] : oven.parts)
        {
            if (id == SALT) { ++salt; continue; }
            if (id == PEPPER) { ++pepper; continue; }
            if (id == SUGAR) { ++sugar; continue; }
            needs.push_back(id);
            const int score = ingredient_score(id, value);
            if (score >= 0) { score_sum += score; ++scored; }
        }
        std::ranges::sort(needs);

        short result = BLAND_MUSH;
        int quality = 0;
        for (const oven_recipe &r : oven_recipes())
        {
            if (r.needs != needs) continue;
            quality = (scored > 0) ? score_sum / scored : 100;
            const int spice_off = std::abs(salt - r.salt) + std::abs(pepper - r.pepper) +
                ((r.kind == "butter_lamb") ? 0 : std::abs(sugar - r.sugar)); // @note the Butter Lamb's sugar picks which lamb it is
            quality = std::clamp(quality - spice_off * SPICE_PENALTY, 0, 100);

            const int edible = (oven_id == MASTER_CHEFS_OVEN) ? 50 : 60; // @note "edible food can be made at 50% or above, instead of 60%"
            if (quality < edible) { result = DISGUSTING_MESS; break; }
            result = r.result;
            if (r.kind == "gingerbread" && quality >= 95) result = GINGERBREAD_MAN;
            else if (r.kind == "magnifico") result = (quality >= 96) ? 6050/*Perfecto*/ : (quality >= 85) ? 6048/*Sabroso*/ : 6046/*Bueno*/;
            else if (r.kind == "butter_lamb" && sugar < 2) result = FAILED_BUTTER_LAMB;
            break;
        }

        oven.parts.clear();
        oven.heat = 0;
        world_tile_update(world, oven.pos);

        short count = 1;
        if (oven_id == REPLICATOR && result != BLAND_MUSH && result != DISGUSTING_MESS && RandomRange(1, 100) <= REPLICATOR_CHANCE) count = 2;
        const u_short left = modify_item_inventory(event, ::slot(result, count));
        if (left > 0) add_drop(event, ::slot(result, static_cast<short>(left)), pPeer->pos, world);

        const ::item &made = id_to_item(result);
        std::string text;
        if (result == BLAND_MUSH) text = "`wThat's not a recipe... you made `4Bland Mush``.``";
        else if (result == DISGUSTING_MESS) text = std::format("`wYou cooked it badly ({}%) and made a `4Disgusting Mess``.``", quality);
        else text = std::format("`wYou made {}`2{}`` (`2{}%`` quality)!``", (count > 1) ? "TWO " : "", made.raw_name, quality);
        bubble(event, text);
        on::ConsoleMessage(event.peer, text);
        play_sfx(event.peer, (result == BLAND_MUSH || result == DISGUSTING_MESS) ? "audio/bleep_fail.wav" : "audio/cumbia_horns.wav");
        if (result != BLAND_MUSH && result != DISGUSTING_MESS) pPeer->add_xp(event, 1 + quality / 10);
        world.save();
    }

    bool may_use(::world &world, const ::peer &peer, const ::pos &pos, const ::block &block)
    {
        return peer.role >= MODERATOR || (block.state[2] & S_PUBLIC) || world.can_edit(peer, pos, false);
    }
}

std::vector<std::pair<short, int>> oven_parts_now(const ::oven &oven)
{
    std::vector<std::pair<short, int>> now = oven.parts;
    const long long ms = std::max(0ll, unix_ms() - oven.since);
    const long long heat = static_cast<long long>(oven.heat) * ms / 1000;
    for (auto &[id, value] : now)
    {
        const int cook = id_to_item(id).ingredient;
        if (cook != 0 && cook != -1) value = static_cast<int>(std::min<long long>(value + heat, 1'000'000));
    }
    return now;
}

bool cooking_tile_change(ENetEvent &event, ::world &world, ::gamePacket &gamePacket, ::block &block)
{
    ::peer *pPeer = peer_of(event.peer);
    if (block.fg == 0) return false;
    const ::item &fg = id_to_item(block.fg);
    const ::pos pos = gamePacket.punch;

    if (fg.type == type::CHEMICAL_COMBINER)
    {
        if (gamePacket.id != 18 || !(block.state[2] & S_TOGGLE)/*closed*/) return false;
        if (!may_use(world, *pPeer, pos, block)) return false;
        return combine(event, world, pos, block); // @note nothing to make: the punch opens/closes it like any punch
    }
    if (fg.type != type::COOKING_OVEN) return false;

    ::oven *oven = oven_at(world, pos);
    if (gamePacket.id == 18)
    {
        if (oven == nullptr || oven->parts.empty()) return false; // @note empty: punching damages it
        if (!may_use(world, *pPeer, pos, block)) return false;
        cook(event, world, *oven, block.fg);
        return true;
    }
    if (gamePacket.id == 32) return false; // @note wrench

    const ::item &ingredient = id_to_item(gamePacket.id);
    if (ingredient.ingredient == -1) return false; // @note not an ingredient: whatever else this item does on a block
    if (!may_use(world, *pPeer, pos, block)) throw std::runtime_error("You don't have access to this oven.");

    if (oven == nullptr || oven->parts.empty())
    {
        oven_heat_dialog(event, pos, gamePacket.id, block.fg);
        return true;
    }
    if (oven->parts.size() >= OVEN_MAX_PARTS) throw std::runtime_error("The oven is full!");
    add_ingredient(event, world, *oven, gamePacket.id);
    return true;
}

void cooking_placed(::world &world, const ::pos &pos, const ::item &item)
{
    cooking_broken(world, pos);
    if (item.type == type::CHEMICAL_COMBINER) world.blocks[cord(pos.x_int(), pos.y_int())].state[2] |= S_TOGGLE; // @note a new combiner stands open
}

void cooking_broken(::world &world, const ::pos &pos)
{
    std::erase_if(world.ovens, [&](const ::oven &o) { return o.pos == pos; });
}

void oven_heat(ENetEvent &event, const ::hPipe &hPipe)
{
    ::peer *pPeer = peer_of(event.peer);
    if (pPeer->netid == 0) return;
    auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
    if (world == worlds.end()) return;

    const std::string button = hPipe["buttonClicked"];
    const u_char heat = (button == "low") ? 3 : (button == "medium") ? 6 : (button == "high") ? 9 : 0; // @note the game shows 3 as Low, 6 as Medium, more as High
    if (heat == 0) return;

    const ::pos pos{ atoi(hPipe["tilex"].c_str()), atoi(hPipe["tiley"].c_str()) };
    if (!valid_tile(*world, pos)) return;
    ::block &block = world->blocks[cord(pos.x_int(), pos.y_int())];
    if (id_to_item(block.fg).type != type::COOKING_OVEN || !may_use(*world, *pPeer, pos, block)) return;

    const short id = static_cast<short>(atoi(hPipe["ingredient"].c_str()));
    if (id_to_item(id).ingredient == -1 || pPeer->have_slot(::slot(id, 0)) == 0) return;

    ::oven *oven = oven_at(*world, pos);
    if (oven == nullptr) oven = &world->ovens.emplace_back(::oven{ .pos = pos });
    if (!oven->parts.empty()) return; // @note someone else started cooking meanwhile
    oven->heat = heat;
    oven->since = unix_ms();
    add_ingredient(event, *world, *oven, id);
}
