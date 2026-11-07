#include "pch.hpp"

#include "database/database.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "onVariant/SetClothing.hpp"
#include "tools/random.hpp"

#include "item_effects.hpp"

#include <algorithm>
#include <unordered_map>

namespace
{
    std::unordered_map<u_short, ::item_effect> table{};

    int number(const char *cell, int low, int high)
    {
        return std::clamp(cell ? std::atoi(cell) : 0, low, high);
    }

    void load()
    {
        if (mysql_query(db.mysql, "SELECT item, gems, xp, one_hit, punch_range, build_range, drops, harvest, bonus_item, bonus_chance, bonus_amount FROM item_effects WHERE enabled = 1")) return;
        MYSQL_RES *res = mysql_store_result(db.mysql);
        if (res == nullptr) return;
        std::unordered_map<u_short, ::item_effect> loaded{};
        while (MYSQL_ROW row = mysql_fetch_row(res))
        {
            const int id = number(row[0], 0, 65535);
            if (id <= 0) continue;
            loaded[static_cast<u_short>(id)] = ::item_effect{
                .item = static_cast<u_short>(id),
                .gems = number(row[1], 0, item_effects::MAX_PERCENT),
                .xp = number(row[2], 0, item_effects::MAX_PERCENT),
                .one_hit = row[3] && row[3][0] == '1',
                .punch_range = number(row[4], 0, item_effects::MAX_RANGE),
                .build_range = number(row[5], 0, item_effects::MAX_RANGE),
                .drops = number(row[6], 0, 1000),
                .harvest = number(row[7], 0, 1000),
                .bonus_item = static_cast<u_short>(number(row[8], 0, 65535)),
                .bonus_chance = number(row[9], 0, 10000),
                .bonus_amount = number(row[10], 1, 200),
            };
            ::item_effect &fx = loaded[static_cast<u_short>(id)];
            if (fx.bonus_item >= items.size() || fx.bonus_item == 112/*Gems*/) fx.bonus_item = 0; // @note gone from items.dat
        }
        mysql_free_result(res);
        table = std::move(loaded);
    }
}

void item_effects::init()
{
    mysql_query(db.mysql,
        "CREATE TABLE IF NOT EXISTS item_effects ("
        "item INT NOT NULL PRIMARY KEY,"
        "gems INT NOT NULL DEFAULT 0," // @note % more gems from breaking
        "xp INT NOT NULL DEFAULT 0," // @note % more XP
        "one_hit TINYINT NOT NULL DEFAULT 0,"
        "punch_range INT NOT NULL DEFAULT 0," // @note extra tiles for breaking
        "build_range INT NOT NULL DEFAULT 0," // @note extra tiles for placing
        "drops INT NOT NULL DEFAULT 0," // @note % chance of one more block or seed
        "harvest INT NOT NULL DEFAULT 0," // @note % more fruit from trees
        "enabled TINYINT NOT NULL DEFAULT 1,"
        "note VARCHAR(120) NOT NULL DEFAULT ''," // @note the website's own note, the game doesn't read it
        "updated TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP"
    ")");
    /* @note added after the first version of the table */
    mysql_query(db.mysql, "ALTER TABLE item_effects ADD COLUMN IF NOT EXISTS bonus_item INT NOT NULL DEFAULT 0");
    mysql_query(db.mysql, "ALTER TABLE item_effects ADD COLUMN IF NOT EXISTS bonus_chance INT NOT NULL DEFAULT 0"); // @note hundredths of a percent
    mysql_query(db.mysql, "ALTER TABLE item_effects ADD COLUMN IF NOT EXISTS bonus_amount INT NOT NULL DEFAULT 1");
    load();
}

void item_effects::reload()
{
    load();
    peers("", PeerSearch::EVERYONE, [](ENetPeer *peer)
    {
        const ::peer *pPeer = static_cast<::peer*>(peer->data);
        if (pPeer != nullptr && pPeer->authenticated && pPeer->netid != 0) on::SetClothing(peer); // @note sends the new reach
    });
}

const ::item_effect *item_effects::get(u_short item)
{
    auto it = table.find(item);
    return it == table.end() ? nullptr : &it->second;
}

::effect_totals item_effects::totals(const std::array<float, 10ull> &clothing)
{
    ::effect_totals sum{};
    for (float cloth : clothing)
    {
        if (cloth <= 0.0f) continue;
        const ::item_effect *fx = get(static_cast<u_short>(cloth));
        if (fx == nullptr) continue;
        sum.gems += fx->gems;
        sum.xp += fx->xp;
        sum.one_hit = sum.one_hit || fx->one_hit;
        sum.punch_range += fx->punch_range;
        sum.build_range += fx->build_range;
        sum.drops += fx->drops;
        sum.harvest += fx->harvest;
    }
    sum.gems = std::min(sum.gems, MAX_PERCENT);
    sum.xp = std::min(sum.xp, MAX_PERCENT);
    sum.punch_range = std::min(sum.punch_range, MAX_RANGE);
    sum.build_range = std::min(sum.build_range, MAX_RANGE);
    sum.drops = std::min(sum.drops, 1000);
    sum.harvest = std::min(sum.harvest, 1000);
    return sum;
}

long long item_effects::boost(long long base, int percent)
{
    if (base <= 0 || percent <= 0) return base;
    const long long scaled = base * (100 + percent); // @note in hundredths
    long long out = scaled / 100;
    if (RandomRange(0, 99) < static_cast<int>(scaled % 100)) ++out;
    return out;
}

int item_effects::extra_rolls(int percent)
{
    if (percent <= 0) return 0;
    int n = percent / 100;
    if (RandomRange(0, 99) < percent % 100) ++n;
    return n;
}

void item_effects::bonus_drops(ENetEvent &event, const ::peer &peer, const ::pos &pos, ::world &world)
{
    for (float cloth : peer.clothing)
    {
        if (cloth <= 0.0f) continue;
        const ::item_effect *fx = get(static_cast<u_short>(cloth));
        if (fx == nullptr || fx->bonus_item == 0 || fx->bonus_chance <= 0) continue;
        if (RandomRange(0, 9999) >= fx->bonus_chance) continue;
        add_break_drop(event, ::slot(static_cast<short>(fx->bonus_item), static_cast<short>(fx->bonus_amount)), pos, world);
    }
}

std::string item_effects::describe(const ::item_effect &fx)
{
    std::vector<std::string> parts{};
    if (fx.gems) parts.emplace_back(std::format("+{}% gems", fx.gems));
    if (fx.xp) parts.emplace_back(std::format("+{}% XP", fx.xp));
    if (fx.one_hit) parts.emplace_back("one-hit breaking");
    if (fx.punch_range) parts.emplace_back(std::format("+{} break range", fx.punch_range));
    if (fx.build_range) parts.emplace_back(std::format("+{} build range", fx.build_range));
    if (fx.drops) parts.emplace_back(std::format("+{}% extra drops", fx.drops));
    if (fx.harvest) parts.emplace_back(std::format("+{}% harvest", fx.harvest));
    if (fx.bonus_item && fx.bonus_chance)
    {
        const std::string chance = (fx.bonus_chance % 100 == 0) ? std::to_string(fx.bonus_chance / 100) : std::format("{:.2f}", fx.bonus_chance / 100.0);
        parts.emplace_back(std::format("{}% chance of {} {}", chance, fx.bonus_amount, id_to_item(fx.bonus_item).raw_name));
    }
    std::string out{};
    for (std::size_t i = 0; i < parts.size(); ++i) out += (i ? ", " : "") + parts[i];
    return out;
}

std::string item_effects::active_lines(const std::array<float, 10ull> &clothing)
{
    std::string list{};
    for (float cloth : clothing)
    {
        if (cloth <= 0.0f) continue;
        const ::item_effect *fx = get(static_cast<u_short>(cloth));
        if (fx == nullptr) continue;
        const std::string text = describe(*fx);
        if (text.empty()) continue;
        list.append(std::format("add_label_with_icon|small|`w{}`` `o({})``|left|{}|\n", text, id_to_item(fx->item).raw_name, fx->item));
    }
    return list;
}

void item_effects::equip_message(ENetPeer *peer, u_short item, bool added)
{
    const ::item_effect *fx = get(item);
    if (fx == nullptr) return;
    const std::string text = describe(*fx);
    if (text.empty()) return;
    on::ConsoleMessage(peer, std::format("`o{}: `${}`` `o(item effect {})``", id_to_item(item).raw_name, text, added ? "added" : "removed"));
}
