#include "pch.hpp"

#include "database/database.hpp"

#include "machines.hpp"

#include <algorithm>
#include <unordered_map>

namespace
{
    struct setting
    {
        int capacity{};
        machines::accepts takes{machines::ANY};
        bool remote{};
    };
    std::unordered_map<short, setting> table{};

    /* @return the real game's numbers. source: github.com/cppdecl/Nomikai Src/World/Tile.cc (magplants) */
    setting defaults(const ::item &item)
    {
        switch (item.type)
        {
            case type::MAGPLANT:
                if (item.id == 6946/*Gaia's Beacon*/) return { 1500, machines::SEEDS_ONLY, false };
                if (item.id == 6948/*Unstable Tesseract*/) return { 1500, machines::NO_SEEDS, false };
                return { 5000, machines::ANY, true };
            case type::AUTO_BREAK: return { 5000, machines::ANY, false }; // @note Techno-Organic Engine
            case type::VENDING_MACHINE: return { 0, machines::ANY, false }; // @note no limit, like before this page
            default: return {};
        }
    }

    int number(const char *cell, int low, int high)
    {
        return std::clamp(cell ? std::atoi(cell) : 0, low, high);
    }

    void load()
    {
        if (mysql_query(db.mysql, "SELECT item, capacity, accepts, remote FROM machines")) return;
        MYSQL_RES *res = mysql_store_result(db.mysql);
        if (res == nullptr) return;
        std::unordered_map<short, setting> loaded{};
        while (MYSQL_ROW row = mysql_fetch_row(res))
        {
            const int id = number(row[0], 0, 65535);
            if (id <= 0 || id >= static_cast<int>(items.size()) || !machines::is_machine(static_cast<short>(id))) continue;
            const ::item &item = id_to_item(static_cast<u_short>(id));
            setting s{
                .capacity = number(row[1], 0, machines::MAX_CAPACITY),
                .takes = static_cast<machines::accepts>(number(row[2], 0, 2)),
                .remote = row[3] && row[3][0] == '1',
            };
            if (item.type != type::VENDING_MACHINE && s.capacity < 1) s.capacity = 1; // @note only vending machines may be unlimited
            if (item.type != type::MAGPLANT) { s.takes = machines::ANY; s.remote = false; }
            loaded[static_cast<short>(id)] = s;
        }
        mysql_free_result(res);
        table = std::move(loaded);
    }
}

bool machines::is_machine(short item)
{
    if (item <= 0) return false;
    const type t = static_cast<type>(id_to_item(static_cast<u_short>(item)).type);
    return t == type::MAGPLANT || t == type::VENDING_MACHINE || t == type::AUTO_BREAK;
}

void machines::init()
{
    mysql_query(db.mysql,
        "CREATE TABLE IF NOT EXISTS machines ("
        "item INT NOT NULL PRIMARY KEY,"
        "capacity INT NOT NULL DEFAULT 5000," // @note 0 = no limit (vending machines)
        "accepts TINYINT NOT NULL DEFAULT 0," // @note magplants: 0 anything, 1 seeds only, 2 no seeds
        "remote TINYINT NOT NULL DEFAULT 0," // @note magplants: building mode and the remote
        "builtin TINYINT NOT NULL DEFAULT 0," // @note 1 = added by the game from items.dat, 0 = made on the website
        "note VARCHAR(120) NOT NULL DEFAULT ''," // @note the website's own note, the game doesn't read it
        "updated TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP"
    ")");
    /* @note every machine items.dat has (custom ones too) gets a row with the real game's numbers, once */
    std::string values{};
    const std::size_t original = items_original_count();
    for (const ::item &item : items)
    {
        if (item.id <= 0 || !is_machine(static_cast<short>(item.id))) continue;
        const setting s = defaults(item);
        values.append(std::format("{}({},{},{},{},{})", values.empty() ? "" : ",", item.id, s.capacity, static_cast<int>(s.takes), s.remote ? 1 : 0,
            static_cast<std::size_t>(item.id) < original ? 1 : 0));
    }
    if (!values.empty()) mysql_query(db.mysql, std::format("INSERT IGNORE INTO machines (item, capacity, accepts, remote, builtin) VALUES {}", values).c_str());
    load();
}

void machines::reload()
{
    load();
}

int machines::capacity(short item)
{
    if (auto it = table.find(item); it != table.end()) return it->second.capacity;
    return defaults(id_to_item(static_cast<u_short>(std::max<short>(item, 0)))).capacity;
}

machines::accepts machines::takes(short item)
{
    if (auto it = table.find(item); it != table.end()) return it->second.takes;
    return defaults(id_to_item(static_cast<u_short>(std::max<short>(item, 0)))).takes;
}

bool machines::has_remote(short item)
{
    if (id_to_item(static_cast<u_short>(std::max<short>(item, 0))).type != type::MAGPLANT) return false;
    if (auto it = table.find(item); it != table.end()) return it->second.remote;
    return defaults(id_to_item(static_cast<u_short>(item))).remote;
}
