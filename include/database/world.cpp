#include "pch.hpp"
#include "friends.hpp"
#include "tools/random.hpp"
#include "tools/time.hpp"
#include "onVariant/ConsoleMessage.hpp"

#include "world.hpp"

#include <algorithm>

char get_type(const ::item &item)
{
    switch (item.type)
    {
        case type::MAIN_DOOR: case type::DOOR: case type::PORTAL: case type::SUNGATE: return '\x01';
        case type::SIGN: return '\x02';
        case type::LOCK: return '\x03';
        case type::SEED: return '\x04';
        case type::RANDOM: return '\x08';
        case type::PROVIDER: return '\x09';
        case type::DISPLAY_BLOCK: return '\x17';
        case type::VENDING_MACHINE: return '\x18';
        case type::STORAGE_BOX: return '\x36';
        case type::XENONITE: return '\x12'; // @note the game crashes on a crystal tile without its extra data
    }
    /*
    * @note every other block the game gives tile data to. a tile like this sent WITHOUT its data crashes the game,
    * so these send empty defaults (write_default_extra) until the block gets a real feature.
    * action type -> extra type, from github.com/voidkle/gtps-guide guide/tile-extras.md (5.60 client)
    */
    switch (static_cast<int>(item.type))
    {
        case 33: return '\x06'; // mailbox
        case 34: case 50: return '\x07'; // bulletin board, security camera
        case 40: return '\x0a'; // achievement block
        case 46: return '\x0b'; // heart monitor
        case 47: return '\x0c'; // donation box
        case 49: return '\x0e'; // mannequin
        case 51: return '\x0f'; // bunny egg
        case 52: return '\x10'; // game blocks
        case 53: return '\x11'; // game generator
        case 55: return '\x13'; // phone booth
        case 56: return '\x14'; // crystal
        case 57: return '\x15'; // crime in progress
        case 59: return '\x16'; // spotlight
        case 63: return '\x19'; // fish tank port
        case 65: return '\x1a'; // solar collector
        case 66: return '\x1b'; // forge
        case 67: return '\x1c'; // giving tree
        case 68: return '\x1d'; // giving tree stump
        case 71: return '\x1e'; // steam organ
        case 72: return '\x1f'; // silkworm
        case 73: return '\x20'; // sewing machine
        case 74: case 86: return '\x21'; // country flag, challenge flags
        case 75: return '\x22'; // lobster trap
        case 76: return '\x23'; // painting easel
        case 77: return '\x24'; // battle pet cage
        case 78: return '\x25'; // pet trainer
        case 79: return '\x26'; // steam engine
        case 80: return '\x27'; // lock-bot
        case 81: return '\x28'; // weather machine (one setting)
        case 82: return '\x29'; // spirit storage unit
        case 83: return '\x2b'; // display shelf
        case 84: return '\x2c'; // VIP entrance
        case 85: return '\x2d'; // challenge timer
        case 87: return '\x2f'; // fish wall mount
        case 88: return '\x30'; // portrait
        case 89: return '\x31'; // weather machine (several settings)
        case 91: return '\x32'; // fossil prep station
        case 92: return '\x33'; // DNA processor
        case 93: return '\x34'; // howler, trickster
        case 96: return '\x35'; // chemsynth tank
        case 98: return '\x37'; // oven
        case 99: return '\x38'; // audio rack
        case 100: return '\x39'; // geiger charger
        case 101: return '\x3a'; // the adventure begins
        case 102: return '\x3b'; // tomb robber
        case 103: case 104: case 105: case 106: return '\x3c'; // balloon-o-matic, team entrances
        case 109: return '\x3d'; // training port
        case 111: return '\x3e'; // MAGPLANT 5000, gaia's beacon, unstable tesseract
        case 113: return '\x3f'; // robots
        case 115: return '\x40'; // lucky token
        case 116: return '\x42'; // growscan 9000
        case 117: return '\x43'; // containment field power node
        case 118: return '\x44'; // spirit board
        case 123: return '\x45'; // tesseract manipulator
        case 124: return '\x46'; // heart of gaia
        case 125: return '\x47'; // techno-organic engine
        case 126: return '\x48'; // storm cloud
        case 127: return '\x49'; // phased blocks
        case 130: return '\x4a'; // safe vault
        case 131: return '\x4b'; // angelic counting cloud
        case 134: return '\x4d'; // infinity weather machine
        case 137: return '\x4e'; // completionist
        case 140: return '\x4f'; // pineapple guzzler
        case 141: return '\x50'; // kranken's blocks
        case 142: return '\x51'; // friends entrance
    }
    return '\x00';
}

void block::reset()
{
    this->fg = 0;
    this->bg = 0;
    for (u_char &s : this->state) s = 0;
}

::blob block::to_blob() const
{
    blob blob;
    blob.i16(this->fg);
    blob.i16(this->bg);
    blob.u8(this->state[0]);
    blob.u8(this->state[1]);
    blob.u8(this->state[2]);
    blob.u8(this->state[3]);

    return blob;
}

::blob object::to_blob() const
{
    blob blob;
    blob.i16(this->id);
    blob.f32(this->pos.x);
    blob.f32(this->pos.y);
    blob.i16(this->count);
    blob.i32(this->uid);

    return blob;
}

::blob door::to_blob() const
{
    blob blob;
    blob.i16(this->label.length());
    for (char c : this->label) blob.u8(c);
    blob.u8('\0');

    return blob;
}

::blob sign::to_blob() const
{
    blob blob;
    blob.i16(this->label.length());
    for (char c : this->label) blob.u8(c);
    blob.u32(this->idk);

    return blob;
}

::blob tree::to_blob(bool seconds) const
{
    blob blob;
    blob.i32((seconds) ? ticks() - this->tick : this->tick);
    blob.u8(this->fruit);

    return blob;
}

/*
* @brief the empty data of a block this server doesn't run yet, exactly as long as the game reads it for a world of version 0.
* fields that only exist in newer world versions are left out. source: gtps-guide guide/tile-extras.md
*/
static void write_default_extra(::blob &blob, char type, const ::item &item)
{
    auto str = [&blob]() { blob.i16(0); }; // @note u16 length 0
    auto u32 = [&blob](u_int v = 0) { blob.u32(v); };
    auto u8  = [&blob](u_char v = 0) { blob.u8(v); };
    switch (type)
    {
        case '\x06': case '\x07': case '\x0c': str(); str(); str(); u8(); break; // three strings + flag
        case '\x0a': u32(); u8(0x7f); break;
        case '\x0b': u32(); str(); break;
        case '\x0e': str(); u8(); u32(); for (int i = 0; i < 9; ++i) blob.i16(0); break; // label, flag, value, 9 clothes
        case '\x0f': case '\x1b': case '\x26': case '\x27': case '\x29': case '\x32': case '\x39': case '\x49': case '\x4e': case '\x4f': u32(); break;
        case '\x10': u8(4); break; // @note 4: no team
        case '\x13': for (int i = 0; i < 9; ++i) blob.i16(0); break; // @note read and thrown away
        case '\x14': str(); break;
        case '\x15': str(); u32(); u8(); break;
        case '\x19': u8(); u32(0); break; // @note 0 fish
        case '\x1a': u8(0x18); u32(); break;
        case '\x1c': u8(); u32(); u8(); break;
        case '\x1e': u8(); u32(); break;
        case '\x1f': u8(); str(); u32(); u32(); u32(); u8(); u32(21600); u32(21600); u32(0xffffffff); u32(); break; // @note fed and watered
        case '\x20': u32(0); break; // @note empty project
        case '\x21': if (item.id == 3394/*Country Flag*/) str(); break;
        case '\x23': u32(); str(); break;
        case '\x24': str(); u32(); u32(); u32(); break;
        case '\x25': str(); u32(0); u32(); break; // @note name, 0 pets, age
        case '\x28': u32(0x4080ff00); break;
        case '\x2b': u32(); u32(); u32(); u32(); break;
        case '\x2c': u8(); u32(); u32(0); break; // @note flags, owner, 0 user ids
        case '\x2f': str(); u32(); u8(); break;
        case '\x30': str(); u32(); u32(); u32(); u32(); blob.i16(0); blob.i16(0); blob.i16(0); break; // @note value 0: no portrait drawn
        case '\x31': u32(2); u32(50); u8(); break;
        case '\x35': u32(); u32(); break;
        case '\x37': u32(); u32(0); u32(); break; // @note heat off, no ingredients, timer
        case '\x38': str(); u32(); break;
        case '\x3c': u32(); u8(static_cast<u_char>(std::clamp(static_cast<int>(item.type) - 103, 0, 3))); break;
        case '\x3d': u32(); u8(); u8(); for (int i = 0; i < 7; ++i) u32(); u8(); break;
        case '\x3e': u32(); u32(); u8(); u8(); u32(); break;
        case '\x3f': u32(0); u32(); u32(); break; // @note no commands
        case '\x40': str(); break;
        case '\x42': u8(); break;
        case '\x43': u32(); u32(0); break;
        case '\x44': u32(); str(); str(); u32(0); break;
        case '\x45': case '\x46': u32(); u32(); u32(); break; // @note the "on" u32 is only read for world version > 7
        case '\x47': u32(); u32(); u32(); u32(); u32(); u8(); u8(); u32(); u32(); break;
        case '\x48': u32(0); u32(1); u32(0); break;
        case '\x4b': u32(); str(); break;
        case '\x4d': u32(); u32(0); break;
        case '\x50': u32(); u32(0xffffffff); break;
        case '\x51': u32(0); u32(1); break;
        default: break; // @note 0x11, 0x16, 0x1d, 0x22, 0x2d, 0x33, 0x34, 0x3a, 0x3b, 0x4a: the game reads no bytes from a server
    }
}

::blob world::tile_blob(int i)
{
    ::blob blob;
    const ::block &block = this->blocks[i];
    const int x = 100;
    const ::pos block_pos{i % x, i / x};
    const ::item &item = id_to_item(block.fg);
    const char type = (block.fg != 0) ? get_type(item) : '\x00';

    ::block copy = block;
    copy.state[2] &= ~S_EXTRA;
    if (type > '\x00') copy.state[2] |= S_EXTRA; // @note real servers flag every tile that carries extra data
    if (is_tile_lock(block.fg)) // @note a public area lock's border is drawn yellow. the game picks red or green itself from the owner and access list
    {
        const auto lock = std::ranges::find(this->area_locks, block_pos, &::area_lock::pos);
        if (lock != this->area_locks.end() && lock->is_public) copy.state[2] |= S_PUBLIC;
        else copy.state[2] &= ~S_PUBLIC;
    }
    if (copy.state[2] & S_PARENT) // @note a parent that isn't an area lock any more would crash the game
    {
        const int parent = copy.state[0] | (copy.state[1] << 8);
        if (parent < 0 || parent >= static_cast<int>(this->blocks.size()) || !is_tile_lock(this->blocks[parent].fg)) copy.state[2] &= ~S_PARENT;
    }
    if (!(copy.state[2] & S_PARENT)) copy.state[0] = copy.state[1] = 0;
    const auto heart = (type == '\x0b') ? std::ranges::find(this->heart_monitors, block_pos, &::heart_monitor::pos) : this->heart_monitors.end();
    if (type == '\x0b') // @note the heart beats (tile flag 0x40) while its player is online
    {
        if (heart != this->heart_monitors.end() && online_by_uid(heart->uid) != nullptr) copy.state[2] |= S_TOGGLE;
        else copy.state[2] &= ~S_TOGGLE;
    }
    blob.push_back(copy.to_blob());

    if (copy.state[2] & S_PARENT) blob.i16(static_cast<short>(copy.state[0] | (copy.state[1] << 8))); // @note the lock that owns this tile

    if (type == '\x00') return blob;
    blob.u8(type);
    switch (type)
    {
        case '\x01': /* doors, portal */
        {
            if (block.fg == 6/*Main Door*/) this->spawn = block_pos.by_32(false);

            auto door = std::ranges::find(this->doors, block_pos, &::door::pos);
            blob.push_back((door != this->doors.end()) ? door->to_blob() : ::door("", "", "", block_pos).to_blob()); // @note data must always follow this tile type
            break;
        }
        case '\x02': /* sign */
        {
            auto sign = std::ranges::find(this->signs, block_pos, &::sign::pos);
            blob.push_back((sign != this->signs.end()) ? sign->to_blob() : ::sign("", block_pos).to_blob());
            break;
        }
        case '\x03': /* lock: settings, owner, access count, access */
        {
            if (is_tile_lock(block.fg))
            {
                auto lock = std::ranges::find(this->area_locks, block_pos, &::area_lock::pos);
                if (lock != this->area_locks.end())
                {
                    blob.u8(0x01/*area lock*/ | (lock->ignore_air ? 0x20/*ignore empty air*/ : 0x00)); // @note the same bits as the World Lock's music bits; source: github.com/cppdecl/Nomikai Src/World/REFERENCE_enums.h
                    blob.i32(lock->owner);
                    blob.i32(static_cast<int>(lock->access.size()));
                    for (const int uid : lock->access) blob.i32(uid);
                }
                else
                {
                    blob.u8(0x01/*area lock*/);
                    blob.i32(0);
                    blob.i32(0);
                }
            }
            else
            {
                this->is_public = (block.state[2] & S_PUBLIC);
                blob.u8(this->lock_state);
                blob.i32(this->owner);
                blob.i32(static_cast<int>(std::ranges::count_if(this->access, [](int uid) { return uid > 0; })));
                for (const int uid : this->access) if (uid > 0) blob.i32(uid); // @note the access list follows its count
            }
            /* @note nothing else follows. a real world (version 12+) adds two u32s here, but this server sends version 0,
             * so the game reads no more bytes. sending them shifted every later tile and broke worlds with locks.
             * source: github.com/voidkle/gtps-guide guide/tile-extras.md (0x03 Lock) */
            break;
        }
        case '\x04': /* seed */
        {
            auto tree = std::ranges::find(this->trees, block_pos, &::tree::pos);
            blob.push_back((tree != this->trees.end()) ? tree->to_blob(true) : ::tree(ticks(), 0, block_pos).to_blob(true));
            break;
        }
        case '\x08': /* dice */
        {
            auto random = std::ranges::find(this->random_blocks, block_pos, &::random_block::pos);
            blob.u8((random != this->random_blocks.end()) ? random->value : 0);
            break;
        }
        case '\x09': /* provider: seconds since it was last collected */
        {
            auto prov = std::ranges::find(this->providers, block_pos, &::provider::pos);
            blob.u32((prov != this->providers.end()) ? ticks() - prov->tick : static_cast<u_int>(item.tick)); // @note one placed before timers is ready
            if (block.fg == 5318/*Winterfest Calendar - 2017*/) blob.u32(0); // @note the game reads one more u32 for this one
            break;
        }
        case '\x12': /* xenonite crystal: u8 + u32 (gtps-guide tile-extras 0x12). the powers are applied by the server, so both stay 0 */
        {
            blob.u8(0);
            blob.u32(0);
            break;
        }
        case '\x29': /* spirit storage unit: ghosts stored */
        {
            auto ssu = std::ranges::find(this->spirit_units, block_pos, &::spirit_unit::pos);
            blob.u32((ssu != this->spirit_units.end()) ? static_cast<u_int>(ssu->ghosts) : 0);
            break;
        }
        case '\x17': /* display block */
        {
            auto display = std::ranges::find(this->displays, block_pos, &::display::pos);
            blob.u32((display != this->displays.end()) ? display->id : 0);
            break;
        }
        case '\x18': /* vending machine */
        {
            auto vend = std::ranges::find(this->vendings, block_pos, &::vending::pos);
            const bool stocked = vend != this->vendings.end() && vend->count > 0;
            blob.u32(stocked ? static_cast<u_int>(vend->id) : 0);
            blob.i32(stocked ? vend->price : 0);
            break;
        }
        case '\x0b': /* heart monitor: user id, name */
        {
            blob.i32((heart != this->heart_monitors.end()) ? heart->uid : 0);
            const std::string name = (heart != this->heart_monitors.end()) ? heart->name : "";
            blob.i16(static_cast<short>(name.length()));
            for (char c : name) blob.u8(c);
            break;
        }
        case '\x0e': /* mannequin: label, flag, value, 9 clothes (hair .. charm) */
        {
            auto dummy = std::ranges::find(this->mannequins, block_pos, &::mannequin::pos);
            const std::string label = (dummy != this->mannequins.end()) ? dummy->label : "";
            blob.i16(static_cast<short>(label.length()));
            for (char c : label) blob.u8(c);
            blob.u8(0);
            blob.u32(0);
            for (int i = 0; i < 9; ++i) blob.i16((dummy != this->mannequins.end()) ? dummy->clothes[i] : 0);
            break;
        }
        case '\x28': /* weather machine with one setting: for the Background machine, the background's item id */
        {
            auto w = std::ranges::find(this->weather_settings, block_pos, &::weather_setting::pos);
            blob.u32((w != this->weather_settings.end()) ? w->item : 0x4080ff00);
            break;
        }
        case '\x31': /* weather machine with several settings: item, gravity, flags */
        {
            auto w = std::ranges::find(this->weather_settings, block_pos, &::weather_setting::pos);
            blob.u32((w != this->weather_settings.end()) ? w->item : 2);
            blob.i32((w != this->weather_settings.end()) ? w->gravity : 50);
            blob.u8(0);
            break;
        }
        case '\x2b': /* display shelf: the four items */
        {
            auto shelf = std::ranges::find(this->shelves, block_pos, &::shelf::pos);
            for (int i = 0; i < 4; ++i) blob.u32((shelf != this->shelves.end()) ? shelf->items[i] : 0);
            break;
        }
        case '\x19': /* fish tank port: flags, then (fish, pounds) pairs */
        {
            auto tank = std::ranges::find(this->fish_tanks, block_pos, &::fish_tank::pos);
            const std::size_t n = (tank != this->fish_tanks.end()) ? tank->fish.size() : 0;
            blob.u8(0);
            blob.u32(static_cast<u_int>(n * 2));
            for (std::size_t i = 0; i < n; ++i) { blob.u32(static_cast<u_int>(tank->fish[i].first)); blob.u32(static_cast<u_int>(tank->fish[i].second)); }
            break;
        }
        case '\x2f': /* fish wall mount: label, fish, pounds */
        {
            auto mount = std::ranges::find(this->fish_mounts, block_pos, &::fish_mount::pos);
            const bool found = mount != this->fish_mounts.end();
            const std::string label = found ? mount->label : "";
            blob.i16(static_cast<short>(label.length()));
            for (char c : label) blob.u8(c);
            blob.u32(found ? static_cast<u_int>(mount->fish) : 0);
            blob.u8(found ? static_cast<u_char>(std::clamp(mount->lbs, 0, 255)) : 0);
            break;
        }
        case '\x42': /* growscan 9000: usable by public */
        {
            blob.u8((block.state[2] & S_PUBLIC) ? 1 : 0);
            break;
        }
        case '\x3e': /* magplant: item, count, collecting, building mode, capacity */
        {
            auto mag = std::ranges::find(this->magplants, block_pos, &::magplant::pos);
            const bool found = mag != this->magplants.end();
            blob.u32(found ? static_cast<u_int>(mag->item) : 0);
            blob.u32(found ? static_cast<u_int>(mag->count) : 0);
            blob.u8(found && mag->collect);
            blob.u8(found && mag->remote);
            blob.u32(static_cast<u_int>(magplant_limit(block.fg)));
            break;
        }
        case '\x45': /* tesseract manipulator / heart of gaia: gems, ?, target (the "on" u32 is only read for world version > 7) */
        case '\x46':
        case '\x47': /* techno-organic engine: the same, then its storage: item, count, ?, look, target offset, ? */
        {
            auto m = std::ranges::find(this->auto_machines, block_pos, &::auto_machine::pos);
            const bool found = m != this->auto_machines.end();
            blob.u32(found ? static_cast<u_int>(m->gems) : 0);
            blob.u32(0);
            blob.u32(found ? static_cast<u_int>(m->target) : 0);
            if (type == '\x47')
            {
                blob.u32(found ? static_cast<u_int>(m->item) : 0);
                blob.u32(found ? static_cast<u_int>(m->count) : 0);
                blob.u8(0);
                blob.u8(found && m->count > 0);
                blob.u32(0);
                blob.u32(0);
            }
            break;
        }
        case '\x32': /* fossil prep station: seconds left */
        case '\x39': /* geiger charger: seconds charged so far */
        {
            auto m = std::ranges::find(this->timed_machines, block_pos, &::timed_machine::pos);
            const long long now = std::time(nullptr);
            u_int value{};
            if (m != this->timed_machines.end())
                value = static_cast<u_int>((type == '\x32') ? std::max(0ll, m->ready - now) : std::clamp(now - m->start, 0ll, m->ready - m->start));
            blob.u32(value);
            break;
        }
        case '\x10': /* game blocks: the team (4 = none) */
        {
            auto t = std::ranges::find(this->game_tiles, block_pos, &::game_tile::pos);
            blob.u8((t != this->game_tiles.end()) ? t->team : 4);
            break;
        }
        case '\x37': /* oven: heat, (ingredient, heat taken) pairs, seconds since this update. the game keeps cooking on its own */
        {
            auto o = std::ranges::find(this->ovens, block_pos, &::oven::pos);
            const bool found = o != this->ovens.end() && !o->parts.empty();
            blob.u32(found ? o->heat : 0);
            if (!found) { blob.u32(0); blob.u32(0); break; }
            const std::vector<std::pair<short, int>> now = oven_parts_now(*o);
            blob.u32(static_cast<u_int>(now.size() * 2));
            for (const auto &[id, value] : now) { blob.u32(static_cast<u_int>(id)); blob.u32(static_cast<u_int>(value)); }
            blob.u32(0);
            break;
        }
        case '\x50': /* kranken's blocks: value (0 when placed), tint */
        {
            auto k = std::ranges::find(this->krankens, block_pos, &::kranken::pos);
            blob.u32(0);
            blob.u32((k != this->krankens.end()) ? k->color : 0xffffffff);
            break;
        }
        case '\x36': /* storage box: the client only needs to know it exists */
        {
            blob.i16(0);
            break;
        }
        default: write_default_extra(blob, type, item); break;
    }
    return blob;
}

::blob world::serialize()
{
    blob blob;
    blob.i16(0x00); // @todo my rgt world says: 19 00
    blob.i32(0x00); // @todo my rgt world says: 40 00 00 00
    blob.i16(this->name.length());
    for (char c : this->name) blob.u8(c);

    const int y = this->blocks.size() / 100;
    const int x = this->blocks.size() / y;
    blob.i32(x);
    blob.i32(y);
    blob.i16(this->blocks.size());

    /*@todo*/
    blob.i32(0x00);
    blob.i16(0x00);
    blob.u8(0x00);

    for (int i = 0; i < static_cast<int>(this->blocks.size()); ++i)
        blob.push_back(this->tile_blob(i));

    /*@todo*/
    blob.i32(0x00);
    blob.i32(0x00);
    blob.i32(0x00);

    blob.i32(this->objects.size()); // @note number of drops
    blob.i32(this->last_object_uid);
    for (const ::object &object : this->objects) 
    {
        blob.push_back(object.to_blob());
    }
    return blob;
}

bool world::exists(const std::string& name)
{
    ::hStmt hStmt{ "SELECT 1 FROM world WHERE name = ? LIMIT 1" };

    MYSQL_BIND param = make_bind_in(name); // WHERE
    hStmt.bind_param(&param);
    hStmt.execute();

    return (!mysql_stmt_store_result(hStmt.pStmt) && mysql_stmt_num_rows(hStmt.pStmt) > 0);
}

template<typename T>
void world::mysql_insert(const std::string& column, const T& value)
{
    ::hStmt hStmt{ std::format("INSERT INTO world ({}) VALUES (?)", column).c_str() };

    MYSQL_BIND param = make_bind_in(value); // VALUES
    hStmt.bind_param(&param);
    hStmt.execute();
}
template void world::mysql_insert<signed>(const std::string&, const signed&);
template void world::mysql_insert<unsigned>(const std::string&, const unsigned&);
template void world::mysql_insert<float>(const std::string&, const float&);
template void world::mysql_insert<std::string>(const std::string&, const std::string&);
template void world::mysql_insert<::blob>(const std::string&, const ::blob&);

template<typename T>
void world::mysql_update(const std::string& column, const T& value)
{
    ::hStmt hStmt{ std::format("UPDATE world SET {} = ? WHERE name = ?", column).c_str() };

    MYSQL_BIND params[2] = {
        make_bind_in(value),      // SET
        make_bind_in(this->name) // WHERE
    };
    hStmt.bind_param(params);
    hStmt.execute();
}
template void world::mysql_update<signed>(const std::string&, const signed&);
template void world::mysql_update<unsigned>(const std::string&, const unsigned&);
template void world::mysql_update<float>(const std::string&, const float&);
template void world::mysql_update<std::string>(const std::string&, const std::string&);
template void world::mysql_update<::blob>(const std::string&, const ::blob&);

template<typename T>
T world::mysql_select(const std::string &column, const std::string &arg)
{
    T value{};
    ::hStmt hStmt{ std::format("SELECT {}({}) FROM world WHERE name = ? LIMIT 1", arg, column).c_str() };

    MYSQL_BIND param = make_bind_in(this->name); // WHERE
    hStmt.bind_param(&param);
    hStmt.execute();

    u_long length = 0;
    MYSQL_BIND result = make_bind_out(value);
    result.length = &length;
    mysql_stmt_bind_result(hStmt.pStmt, &result);

    hStmt.execute();
    hStmt.fetch();
    if constexpr (std::is_same_v<T, std::string>)
        value.resize(length);
    else if constexpr (std::is_same_v<T, ::blob>)
        value.resize(length);

    return value;
}

void world::mysql_select_all()
{
    this->name = this->mysql_select<std::string>("name");
    this->owner = this->mysql_select<int>("owner");
    this->minimum_entry_level = static_cast<u_char>(std::clamp(this->mysql_select<int>("min_level"), 1, 125));
    {
        std::size_t i = 0;
        for (const std::string &uid : readch(this->mysql_select<std::string>("access"), ','))
            if (const int id = atoi(uid.c_str()); id > 0 && i < this->access.size()) this->access[i++] = id;
    }
    {
        ::blob blob = this->mysql_select<::blob>("blocks");
        if (blob.size() == 0ull) // @note the server stopped before this world was ever saved
        {
            generate_world(*this);
            return;
        }

        this->blocks.resize(cord(0, 60));
        const int x = this->blocks.size() / 60;
        int pos{};
        for (u_short i = 0; ::block &block : this->blocks)
        {
            blob.read_i16(block.fg, pos);
            blob.read_i16(block.bg, pos);
            blob.read_u8(block.state[0], pos);
            blob.read_u8(block.state[1], pos);
            blob.read_u8(block.state[2], pos);
            blob.read_u8(block.state[3], pos);

            if (block.fg != 0 || block.fg!=2||block.fg!=4||block.fg!=8||block.fg!=14) // @note so we can save time
            if (char type = get_type(id_to_item(block.fg)); type > '\x00')
            {
                const ::pos block_pos{i % x, i / x};
                if (type == '\x01'/*doors, portal*/)
                {
                    ::door &door = this->doors.emplace_back("","","", block_pos);

                    short len{};
                    blob.read_i16(len, pos);
                    if (len < 0 || pos + len > (int)blob.size()) len = 0; // @note damaged save
                    door.label.resize(len);
                    for (char &c : door.label) blob.read_u8((u_char&)c, pos);
                    pos++;// @note \0
                }
                else if (type == '\x02'/*sign*/)
                {
                    ::sign &sign = this->signs.emplace_back("", block_pos);

                    short len{};
                    blob.read_i16(len, pos);
                    if (len < 0 || pos + len > (int)blob.size()) len = 0; // @note damaged save
                    sign.label.resize(len);
                    for (char &c : sign.label) blob.read_u8((u_char&)c, pos);
                    blob.read_u32(sign.idk, pos);
                }
                else if (type == '\x04'/*seed*/)
                {
                    ::tree &tree = this->trees.emplace_back(0, 0, block_pos);

                    blob.read_u32(tree.tick, pos);
                    blob.read_u8(tree.fruit, pos);
                    if (tree.tick > ticks()) tree.tick = ticks(); // @note a timer from the future would never finish
                }
            }
            ++i;
        }
    } // @note delete blob, i
    {
        ::blob blob = this->mysql_select<::blob>("objects"); // @note ::blob is cut to the real length, std::vector<u_char> stays at the full buffer size
        
        int pos{};

        blob.read_u32(this->last_object_uid, pos); // @todo real gt has this as 8 bits not just 4.
        objects.resize(blob.size() >= sizeof(u_int) ? (blob.size() - sizeof(u_int)) / 16 : 0); // @note count from the saved data, not the last drop uid
        for (::object &object : this->objects)
        {
            blob.read_u16(object.id, pos);
            blob.read_f32(object.pos.x, pos);
            blob.read_f32(object.pos.y, pos);
            blob.read_u16(object.count, pos);
            blob.read_u32(object.uid, pos);
        }
    } // @note delete blob, pos
    {
        ::blob blob = this->mysql_select<::blob>("extras");
        this->load_extras(blob);
        if (std::ranges::find(this->blocks, 2072/*Xenonite Crystal*/, &::block::fg) == this->blocks.end()) this->xenonite = 0;
    }
}

world::world(const std::string &name) : name(name)/*DEFAULT*/
{
    if (this->exists(this->name)) 
    {
        this->mysql_select_all();
    }
    else 
    {
        this->mysql_insert("name", this->name); // @note DEFAULT
        generate_world(*this);
    }
}
world::~world()
{
    this->save();
}

void world::save()
{
    if (this->blocks.empty()) return; // @note never overwrite a saved world with nothing

    this->mysql_update("owner", this->owner);
    this->mysql_update("min_level", static_cast<int>(this->minimum_entry_level));
    {
        std::string access{};
        for (const int uid : this->access)
            if (uid > 0) access += (access.empty() ? "" : ",") + std::to_string(uid);
        this->mysql_update("access", access);
    }
    {
        ::blob blob;

        const int x = this->blocks.size() / 60;
        for (u_short i = 0; const ::block &block : this->blocks)
        {
            blob.push_back(block.to_blob());

            if (block.fg != 0) // @note so we can save time
            if (u_char type = get_type(id_to_item(block.fg)); type > 0x00)
            {
                const ::pos block_pos{i % x, i / x};
                if (type == '\x01'/*doors, portal*/)
                {
                    auto door = std::ranges::find(this->doors, block_pos, &::door::pos);
                    blob.push_back((door != this->doors.end()) ? door->to_blob() : ::door("", "", "", block_pos).to_blob()); // @note loading always reads this data
                }
                else if (type == '\x02'/*sign*/)
                {
                    auto sign = std::ranges::find(this->signs, block_pos, &::sign::pos);
                    blob.push_back((sign != this->signs.end()) ? sign->to_blob() : ::sign("", block_pos).to_blob());
                }
                else if (type == '\x04'/*seed*/)
                {
                    auto tree = std::ranges::find(this->trees, block_pos, &::tree::pos);
                    blob.push_back((tree != this->trees.end()) ? tree->to_blob() : ::tree(ticks(), 0, block_pos).to_blob());
                }
            }
            ++i;
        }
        this->mysql_update("blocks", blob);
    }
    {
        ::blob blob{};

        blob.i32(this->last_object_uid); // @todo add the other 4 bits like real growtopia
        for (::object &object : this->objects)
        {
            blob.push_back(object.to_blob());
        }
        this->mysql_update("objects", blob.data());
    }
    this->mysql_update("extras", this->extras_blob());
}

namespace
{
    void write_string(::blob &blob, const std::string &text)
    {
        blob.i16(static_cast<short>(std::min<std::size_t>(text.size(), 0x7fff)));
        for (std::size_t i = 0; i < text.size() && i < 0x7fff; ++i) blob.u8(text[i]);
    }
    bool read_string(::blob &blob, int &pos, std::string &text)
    {
        short len{};
        if (!blob.read_i16(len, pos) || len < 0 || pos + len > static_cast<int>(blob.size())) return false;
        text.resize(len);
        for (char &c : text) blob.read_u8(reinterpret_cast<u_char&>(c), pos);
        return true;
    }
    enum extra_kind : u_char { EX_DOOR = 1, EX_AREA_LOCK = 2, EX_VENDING = 3, EX_STORAGE = 4, EX_DISPLAY = 5, EX_XENONITE = 6, EX_PROVIDER = 7, EX_MAILBOX = 8, EX_DONATION = 9, EX_SHELF = 10, EX_HEART = 11, EX_MANNEQUIN = 12, EX_WEATHER = 13, EX_MAGPLANT = 14, EX_FISH_TANK = 15, EX_FISH_MOUNT = 16, EX_LOBSTER_TRAP = 17, EX_AUTO = 18, EX_BASE_WEATHER = 19, EX_TIMED = 20, EX_KRANKEN = 21, EX_VAULT = 22, EX_GAME_TILE = 23, EX_GAME_RULES = 24, EX_CHALLENGE = 25, EX_WORLD_BAN = 26, EX_OVEN = 27, EX_SPIRIT = 28, EX_GHOSTS = 29 };
}

/* @note door targets, area locks, vending machines, storage boxes and display blocks. one record each: kind, x, y, data */
::blob world::extras_blob() const
{
    ::blob blob;
    blob.u8(0x01); // @note format version
    for (const ::door &door : this->doors)
    {
        if (door.dest.empty() && door.id.empty() && door.is_public) continue;
        blob.u8(EX_DOOR); blob.i16(door.pos.x_int()); blob.i16(door.pos.y_int());
        write_string(blob, door.dest);
        write_string(blob, door.id);
        blob.u8(door.is_public);
    }
    for (const ::area_lock &lock : this->area_locks)
    {
        blob.u8(EX_AREA_LOCK); blob.i16(lock.pos.x_int()); blob.i16(lock.pos.y_int());
        blob.i32(lock.owner);
        blob.u8(lock.is_public); blob.u8(lock.ignore_air); blob.u8(lock.builder_mode);
        blob.i32(static_cast<int>(lock.access.size()));
        for (const int uid : lock.access) blob.i32(uid);
    }
    for (const ::vending &vend : this->vendings)
    {
        blob.u8(EX_VENDING); blob.i16(vend.pos.x_int()); blob.i16(vend.pos.y_int());
        blob.i16(vend.id); blob.i32(vend.count); blob.i32(vend.price); blob.i32(vend.earned);
    }
    for (const ::storage &box : this->storages)
    {
        blob.u8(EX_STORAGE); blob.i16(box.pos.x_int()); blob.i16(box.pos.y_int());
        blob.i32(static_cast<int>(box.items.size()));
        for (const ::slot &slot : box.items) { blob.i16(slot.id); blob.i16(slot.count); }
    }
    for (const ::display &display : this->displays)
    {
        blob.u8(EX_DISPLAY); blob.i16(display.pos.x_int()); blob.i16(display.pos.y_int());
        blob.u32(display.id);
    }
    for (const ::provider &prov : this->providers)
    {
        blob.u8(EX_PROVIDER); blob.i16(prov.pos.x_int()); blob.i16(prov.pos.y_int());
        blob.u32(prov.tick);
    }
    for (const ::mailbox &box : this->mailboxes)
    {
        if (box.letters.empty() && !box.is_public && !box.hide_names) continue;
        blob.u8(EX_MAILBOX); blob.i16(box.pos.x_int()); blob.i16(box.pos.y_int());
        blob.u8(box.is_public); blob.u8(box.hide_names);
        blob.u32(box.next_id);
        blob.i32(static_cast<int>(box.letters.size()));
        for (const ::letter &letter : box.letters)
        {
            blob.u32(letter.id); blob.i32(letter.uid);
            write_string(blob, letter.name);
            write_string(blob, letter.text);
        }
    }
    for (const ::donation_box &box : this->donation_boxes)
    {
        if (box.gifts.empty()) continue;
        blob.u8(EX_DONATION); blob.i16(box.pos.x_int()); blob.i16(box.pos.y_int());
        blob.u32(box.next_id);
        blob.i32(static_cast<int>(box.gifts.size()));
        for (const ::gift &gift : box.gifts)
        {
            blob.u32(gift.id); blob.i16(gift.item); blob.i16(gift.count); blob.i32(gift.uid);
            write_string(blob, gift.name);
            write_string(blob, gift.note);
        }
    }
    for (const ::shelf &shelf : this->shelves)
    {
        if (std::ranges::all_of(shelf.items, [](u_int id) { return id == 0; })) continue;
        blob.u8(EX_SHELF); blob.i16(shelf.pos.x_int()); blob.i16(shelf.pos.y_int());
        for (const u_int id : shelf.items) blob.u32(id);
    }
    for (const ::heart_monitor &heart : this->heart_monitors)
    {
        blob.u8(EX_HEART); blob.i16(heart.pos.x_int()); blob.i16(heart.pos.y_int());
        blob.i32(heart.uid);
        write_string(blob, heart.name);
    }
    for (const ::mannequin &dummy : this->mannequins)
    {
        blob.u8(EX_MANNEQUIN); blob.i16(dummy.pos.x_int()); blob.i16(dummy.pos.y_int());
        write_string(blob, dummy.label);
        for (const short id : dummy.clothes) blob.i16(id);
    }
    for (const ::weather_setting &w : this->weather_settings)
    {
        blob.u8(EX_WEATHER); blob.i16(w.pos.x_int()); blob.i16(w.pos.y_int());
        blob.u32(w.item); blob.i32(w.gravity);
    }
    for (const ::magplant &mag : this->magplants)
    {
        if (mag.item == 0) continue;
        blob.u8(EX_MAGPLANT); blob.i16(mag.pos.x_int()); blob.i16(mag.pos.y_int());
        blob.i16(mag.item); blob.i32(mag.count); blob.u8(mag.collect); blob.u8(mag.remote);
    }
    for (const ::fish_tank &tank : this->fish_tanks)
    {
        if (tank.fish.empty()) continue;
        blob.u8(EX_FISH_TANK); blob.i16(tank.pos.x_int()); blob.i16(tank.pos.y_int());
        blob.i32(static_cast<int>(tank.fish.size()));
        for (const auto &[fish, lbs] : tank.fish) { blob.i16(fish); blob.i32(lbs); }
    }
    for (const ::fish_mount &mount : this->fish_mounts)
    {
        if (mount.fish == 0) continue;
        blob.u8(EX_FISH_MOUNT); blob.i16(mount.pos.x_int()); blob.i16(mount.pos.y_int());
        blob.i16(mount.fish); blob.i32(mount.lbs);
        write_string(blob, mount.label);
    }
    for (const ::lobster_trap &trap : this->lobster_traps)
    {
        if (trap.count <= 0) continue;
        blob.u8(EX_LOBSTER_TRAP); blob.i16(trap.pos.x_int()); blob.i16(trap.pos.y_int());
        blob.i16(trap.bait); blob.i32(trap.count); blob.u32(trap.checked);
    }
    for (const ::auto_machine &m : this->auto_machines)
    {
        if (m.gems <= 0 && m.target == 0 && m.count <= 0) continue;
        blob.u8(EX_AUTO); blob.i16(m.pos.x_int()); blob.i16(m.pos.y_int());
        blob.i32(m.gems); blob.i16(m.target); blob.u8(m.on); blob.i16(m.item); blob.i32(m.count);
    }
    for (const ::timed_machine &m : this->timed_machines)
    {
        if (m.item == 0) continue;
        blob.u8(EX_TIMED); blob.i16(m.pos.x_int()); blob.i16(m.pos.y_int());
        blob.i16(m.item); blob.i64(m.start); blob.i64(m.ready);
    }
    for (const ::kranken &k : this->krankens)
    {
        if (k.color == 0xffffffff) continue;
        blob.u8(EX_KRANKEN); blob.i16(k.pos.x_int()); blob.i16(k.pos.y_int());
        blob.u32(k.color);
    }
    for (const ::safe_vault &vault : this->vaults)
    {
        blob.u8(EX_VAULT); blob.i16(vault.pos.x_int()); blob.i16(vault.pos.y_int());
        write_string(blob, vault.password);
        blob.i32(static_cast<int>(vault.items.size()));
        for (const ::slot &slot : vault.items) { blob.i16(slot.id); blob.i16(slot.count); }
    }
    for (const ::game_tile &t : this->game_tiles)
    {
        if (t.team == 4) continue;
        blob.u8(EX_GAME_TILE); blob.i16(t.pos.x_int()); blob.i16(t.pos.y_int());
        blob.u8(t.team);
    }
    for (const ::game_rules &r : this->game_generators)
    {
        blob.u8(EX_GAME_RULES); blob.i16(r.pos.x_int()); blob.i16(r.pos.y_int());
        blob.u8(r.mode); blob.u8(r.teams); blob.u8(r.minutes); blob.u8(r.goal);
    }
    for (const ::challenge_timer &t : this->challenge_timers)
    {
        blob.u8(EX_CHALLENGE); blob.i16(t.pos.x_int()); blob.i16(t.pos.y_int());
        blob.i32(t.seconds); blob.i32(t.best_ms);
        write_string(blob, t.best_name);
    }
    for (const ::oven &o : this->ovens)
    {
        if (o.parts.empty()) continue;
        blob.u8(EX_OVEN); blob.i16(o.pos.x_int()); blob.i16(o.pos.y_int());
        blob.u8(o.heat); blob.i64(o.since);
        blob.i32(static_cast<int>(o.parts.size()));
        for (const auto &[id, value] : o.parts) { blob.i16(id); blob.i32(value); }
    }
    for (const ::world_ban &ban : this->world_bans)
    {
        if (ban.until <= std::time(nullptr)) continue;
        blob.u8(EX_WORLD_BAN); blob.i16(0); blob.i16(0);
        blob.i32(ban.uid); blob.i64(ban.until);
    }
    for (const ::spirit_unit &ssu : this->spirit_units)
    {
        if (ssu.ghosts <= 0) continue;
        blob.u8(EX_SPIRIT); blob.i16(ssu.pos.x_int()); blob.i16(ssu.pos.y_int());
        blob.i32(ssu.ghosts);
    }
    {
        /* @note the ghosts here (kind and tile only) and when the world was last open, so ghosts can move in while it's empty */
        blob.u8(EX_GHOSTS); blob.i16(0); blob.i16(0);
        blob.i64(std::time(nullptr));
        blob.i32(static_cast<int>(this->ghosts.size()));
        for (const ::ghost &g : this->ghosts) { blob.u8(g.kind); blob.i16(static_cast<short>(g.x / 32.0f)); blob.i16(static_cast<short>(g.y / 32.0f)); }
    }
    if (this->base_weather != 0)
    {
        blob.u8(EX_BASE_WEATHER); blob.i16(0); blob.i16(0);
        blob.i16(static_cast<short>(this->base_weather));
    }
    if (this->xenonite != 0)
    {
        blob.u8(EX_XENONITE); blob.i16(0); blob.i16(0); // @note one per world, the tile doesn't matter
        blob.u32(this->xenonite);
    }
    return blob;
}

void world::load_extras(::blob &blob)
{
    int pos{};
    u_char version{};
    if (!blob.read_u8(version, pos) || version != 0x01) return;

    u_char kind{};
    while (blob.read_u8(kind, pos))
    {
        short x{}, y{};
        if (!blob.read_i16(x, pos) || !blob.read_i16(y, pos)) return;
        const ::pos tile{static_cast<int>(x), static_cast<int>(y)};
        switch (kind)
        {
            case EX_DOOR:
            {
                std::string dest{}, id{};
                u_char is_public{1};
                if (!read_string(blob, pos, dest) || !read_string(blob, pos, id) || !blob.read_u8(is_public, pos)) return;
                auto door = std::ranges::find(this->doors, tile, &::door::pos);
                if (door == this->doors.end()) break; // @note the door is gone
                door->dest = dest, door->id = id, door->is_public = is_public;
                break;
            }
            case EX_AREA_LOCK:
            {
                ::area_lock lock{.pos = tile};
                u_char is_public{}, ignore_air{}, mode{};
                int count{};
                if (!blob.read_i32(lock.owner, pos) || !blob.read_u8(is_public, pos) || !blob.read_u8(ignore_air, pos) || 
                    !blob.read_u8(mode, pos) || !blob.read_i32(count, pos) || count < 0 || count > 1000) return;
                for (int i = 0; i < count; ++i)
                {
                    int uid{};
                    if (!blob.read_i32(uid, pos)) return;
                    lock.access.push_back(uid);
                }
                lock.is_public = is_public, lock.ignore_air = ignore_air, lock.builder_mode = mode;
                if (valid_tile(*this, tile) && is_tile_lock(this->blocks[cord(x, y)].fg))
                {
                    this->area_locks.push_back(std::move(lock));
                }
                break;
            }
            case EX_VENDING:
            {
                ::vending vend{.pos = tile};
                if (!blob.read_i16(vend.id, pos) || !blob.read_i32(vend.count, pos) || !blob.read_i32(vend.price, pos) || !blob.read_i32(vend.earned, pos)) return;
                this->vendings.push_back(vend);
                break;
            }
            case EX_STORAGE:
            {
                ::storage box{.pos = tile};
                int count{};
                if (!blob.read_i32(count, pos) || count < 0 || count > 1000) return;
                for (int i = 0; i < count; ++i)
                {
                    ::slot slot{};
                    if (!blob.read_i16(slot.id, pos) || !blob.read_i16(slot.count, pos)) return;
                    box.items.push_back(slot);
                }
                this->storages.push_back(std::move(box));
                break;
            }
            case EX_DISPLAY:
            {
                u_int id{};
                if (!blob.read_u32(id, pos)) return;
                this->displays.emplace_back(id, tile);
                break;
            }
            case EX_PROVIDER:
            {
                ::provider prov{.pos = tile};
                if (!blob.read_u32(prov.tick, pos)) return;
                this->providers.push_back(prov);
                break;
            }
            case EX_XENONITE:
            {
                if (!blob.read_u32(this->xenonite, pos)) return;
                break;
            }
            case EX_MAILBOX:
            {
                ::mailbox box{.pos = tile};
                u_char is_public{}, hide_names{};
                int count{};
                if (!blob.read_u8(is_public, pos) || !blob.read_u8(hide_names, pos) || !blob.read_u32(box.next_id, pos) ||
                    !blob.read_i32(count, pos) || count < 0 || count > 100) return;
                for (int i = 0; i < count; ++i)
                {
                    ::letter letter{};
                    if (!blob.read_u32(letter.id, pos) || !blob.read_i32(letter.uid, pos) || 
                        !read_string(blob, pos, letter.name) || !read_string(blob, pos, letter.text)) return;
                    box.letters.push_back(std::move(letter));
                }
                box.is_public = is_public, box.hide_names = hide_names;
                this->mailboxes.push_back(std::move(box));
                break;
            }
            case EX_HEART:
            {
                ::heart_monitor heart{.pos = tile};
                if (!blob.read_i32(heart.uid, pos) || !read_string(blob, pos, heart.name)) return;
                this->heart_monitors.push_back(std::move(heart));
                break;
            }
            case EX_MANNEQUIN:
            {
                ::mannequin dummy{.pos = tile};
                if (!read_string(blob, pos, dummy.label)) return;
                for (short &id : dummy.clothes) if (!blob.read_i16(id, pos)) return;
                this->mannequins.push_back(std::move(dummy));
                break;
            }
            case EX_WEATHER:
            {
                ::weather_setting w{.pos = tile};
                if (!blob.read_u32(w.item, pos) || !blob.read_i32(w.gravity, pos)) return;
                this->weather_settings.push_back(w);
                break;
            }
            case EX_MAGPLANT:
            {
                ::magplant mag{.pos = tile};
                u_char collect{}, remote{};
                if (!blob.read_i16(mag.item, pos) || !blob.read_i32(mag.count, pos) || !blob.read_u8(collect, pos) || !blob.read_u8(remote, pos)) return;
                mag.collect = collect, mag.remote = remote;
                if (mag.count < 0) mag.count = 0;
                if (valid_tile(*this, tile) && id_to_item(this->blocks[cord(x, y)].fg).type == type::MAGPLANT) this->magplants.push_back(mag);
                break;
            }
            case EX_FISH_TANK:
            {
                ::fish_tank tank{.pos = tile};
                int count{};
                if (!blob.read_i32(count, pos) || count < 0 || count > 100) return;
                for (int i = 0; i < count; ++i)
                {
                    short fish{}; int lbs{};
                    if (!blob.read_i16(fish, pos) || !blob.read_i32(lbs, pos)) return;
                    tank.fish.emplace_back(fish, lbs);
                }
                if (valid_tile(*this, tile) && id_to_item(this->blocks[cord(x, y)].fg).type == type::FISH_TANK_PORT) this->fish_tanks.push_back(std::move(tank));
                break;
            }
            case EX_FISH_MOUNT:
            {
                ::fish_mount mount{.pos = tile};
                if (!blob.read_i16(mount.fish, pos) || !blob.read_i32(mount.lbs, pos) || !read_string(blob, pos, mount.label)) return;
                if (valid_tile(*this, tile) && id_to_item(this->blocks[cord(x, y)].fg).type == type::FISH_MOUNT) this->fish_mounts.push_back(std::move(mount));
                break;
            }
            case EX_LOBSTER_TRAP:
            {
                ::lobster_trap trap{.pos = tile};
                if (!blob.read_i16(trap.bait, pos) || !blob.read_i32(trap.count, pos) || !blob.read_u32(trap.checked, pos)) return;
                if (valid_tile(*this, tile) && id_to_item(this->blocks[cord(x, y)].fg).type == type::LOBSTER_TRAP) this->lobster_traps.push_back(trap);
                break;
            }
            case EX_TIMED:
            {
                ::timed_machine m{.pos = tile};
                if (!blob.read_i16(m.item, pos) || !blob.read_i64(m.start, pos) || !blob.read_i64(m.ready, pos)) return;
                if (valid_tile(*this, tile))
                {
                    const auto kind = id_to_item(this->blocks[cord(x, y)].fg).type;
                    if (kind == type::FOSSIL_PREP_STATION || kind == type::GEIGER_CHARGER) this->timed_machines.push_back(m);
                }
                break;
            }
            case EX_BASE_WEATHER:
            {
                short id{};
                if (!blob.read_i16(id, pos)) return;
                this->base_weather = static_cast<u_short>(std::max<short>(id, 0));
                break;
            }
            case EX_AUTO:
            {
                ::auto_machine m{.pos = tile};
                u_char on{};
                if (!blob.read_i32(m.gems, pos) || !blob.read_i16(m.target, pos) || !blob.read_u8(on, pos) || !blob.read_i16(m.item, pos) || !blob.read_i32(m.count, pos)) return;
                m.on = on;
                m.gems = std::max(0, m.gems), m.count = std::max(0, m.count);
                if (valid_tile(*this, tile))
                {
                    const auto kind = id_to_item(this->blocks[cord(x, y)].fg).type;
                    if (kind == type::AUTO_BREAK_BLOCK || kind == type::AUTO_BREAK_TREE || kind == type::AUTO_BREAK) this->auto_machines.push_back(m);
                }
                break;
            }
            case EX_SHELF:
            {
                ::shelf shelf{.pos = tile};
                for (u_int &id : shelf.items) if (!blob.read_u32(id, pos)) return;
                this->shelves.push_back(shelf);
                break;
            }
            case EX_DONATION:
            {
                ::donation_box box{.pos = tile};
                int count{};
                if (!blob.read_u32(box.next_id, pos) || !blob.read_i32(count, pos) || count < 0 || count > 100) return;
                for (int i = 0; i < count; ++i)
                {
                    ::gift gift{};
                    if (!blob.read_u32(gift.id, pos) || !blob.read_i16(gift.item, pos) || !blob.read_i16(gift.count, pos) || 
                        !blob.read_i32(gift.uid, pos) || !read_string(blob, pos, gift.name) || !read_string(blob, pos, gift.note)) return;
                    box.gifts.push_back(std::move(gift));
                }
                this->donation_boxes.push_back(std::move(box));
                break;
            }
            case EX_KRANKEN:
            {
                ::kranken k{.pos = tile};
                if (!blob.read_u32(k.color, pos)) return;
                if (valid_tile(*this, tile) && id_to_item(this->blocks[cord(x, y)].fg).type == type::KRANKEN_GALACTIC) this->krankens.push_back(k);
                break;
            }
            case EX_VAULT:
            {
                ::safe_vault vault{.pos = tile};
                int count{};
                if (!read_string(blob, pos, vault.password) || !blob.read_i32(count, pos) || count < 0 || count > 1000) return;
                for (int i = 0; i < count; ++i)
                {
                    ::slot slot{};
                    if (!blob.read_i16(slot.id, pos) || !blob.read_i16(slot.count, pos)) return;
                    vault.items.push_back(slot);
                }
                if (valid_tile(*this, tile) && id_to_item(this->blocks[cord(x, y)].fg).type == type::SAFE_VAULT) this->vaults.push_back(std::move(vault));
                break;
            }
            case EX_GAME_TILE:
            {
                ::game_tile t{.pos = tile};
                if (!blob.read_u8(t.team, pos)) return;
                if (valid_tile(*this, tile) && id_to_item(this->blocks[cord(x, y)].fg).type == type::GAME_BLOCK) this->game_tiles.push_back(t);
                break;
            }
            case EX_GAME_RULES:
            {
                ::game_rules r{.pos = tile};
                if (!blob.read_u8(r.mode, pos) || !blob.read_u8(r.teams, pos) || !blob.read_u8(r.minutes, pos) || !blob.read_u8(r.goal, pos)) return;
                if (valid_tile(*this, tile) && id_to_item(this->blocks[cord(x, y)].fg).type == type::GAME_GENERATOR) this->game_generators.push_back(r);
                break;
            }
            case EX_CHALLENGE:
            {
                ::challenge_timer t{.pos = tile};
                if (!blob.read_i32(t.seconds, pos) || !blob.read_i32(t.best_ms, pos) || !read_string(blob, pos, t.best_name)) return;
                if (valid_tile(*this, tile) && id_to_item(this->blocks[cord(x, y)].fg).type == type::CHALLENGE_TIMER) this->challenge_timers.push_back(std::move(t));
                break;
            }
            case EX_OVEN:
            {
                ::oven o{.pos = tile};
                int count{};
                if (!blob.read_u8(o.heat, pos) || !blob.read_i64(o.since, pos) || !blob.read_i32(count, pos) || count < 0 || count > 64) return;
                for (int i = 0; i < count; ++i)
                {
                    short id{}; int value{};
                    if (!blob.read_i16(id, pos) || !blob.read_i32(value, pos)) return;
                    o.parts.emplace_back(id, value);
                }
                if (valid_tile(*this, tile) && id_to_item(this->blocks[cord(x, y)].fg).type == type::COOKING_OVEN) this->ovens.push_back(std::move(o));
                break;
            }
            case EX_SPIRIT:
            {
                ::spirit_unit ssu{.pos = tile};
                if (!blob.read_i32(ssu.ghosts, pos)) return;
                if (valid_tile(*this, tile) && id_to_item(this->blocks[cord(x, y)].fg).type == type::SPIRIT_STORAGE) this->spirit_units.push_back(ssu);
                break;
            }
            case EX_GHOSTS:
            {
                long long seen{};
                int count{};
                if (!blob.read_i64(seen, pos) || !blob.read_i32(count, pos) || count < 0 || count > 255) return;
                this->ghosts_checked = seen;
                for (int i = 0; i < count; ++i)
                {
                    u_char kind{};
                    short gx{}, gy{};
                    if (!blob.read_u8(kind, pos) || !blob.read_i16(gx, pos) || !blob.read_i16(gy, pos)) return;
                    ::ghost g{.kind = kind};
                    g.x = g.dx = gx * 32.0f + 16.0f;
                    g.y = g.dy = gy * 32.0f + 16.0f;
                    this->ghosts.push_back(g); // @note action/ghosts.cpp gives it an NPC id when someone comes in
                }
                break;
            }
            case EX_WORLD_BAN:
            {
                ::world_ban ban{};
                if (!blob.read_i32(ban.uid, pos) || !blob.read_i64(ban.until, pos)) return;
                if (ban.until > std::time(nullptr)) this->world_bans.push_back(ban);
                break;
            }
            default: return; // @note unknown record, stop rather than misread the rest
        }
    }
}

::area_lock *world::lock_at(const ::pos &pos)
{
    if (!valid_tile(*this, pos)) return nullptr;
    const ::block &block = this->blocks[cord(pos.x_int(), pos.y_int())];

    ::pos lock_pos = pos;
    if (!is_tile_lock(block.fg)) // @note the lock's own tile belongs to it too
    {
        if (!(block.state[2] & S_PARENT)) return nullptr;
        const int index = block.state[0] | (block.state[1] << 8);
        lock_pos = ::pos{index % 100, index / 100};
    }
    auto lock = std::ranges::find(this->area_locks, lock_pos, &::area_lock::pos);
    return (lock != this->area_locks.end()) ? &*lock : nullptr;
}

void world::release_area_lock(const ::pos &lock_pos)
{
    const int index = cord(lock_pos.x_int(), lock_pos.y_int());
    for (::block &block : this->blocks)
    {
        if ((block.state[2] & S_PARENT) && (block.state[0] | (block.state[1] << 8)) == index)
        {
            block.state[2] &= ~S_PARENT;
            block.state[0] = block.state[1] = 0;
        }
    }
}

/* @note spreads out from the lock one step at a time (like real Growtopia), skipping tiles another lock already holds */
void world::apply_area_lock(::area_lock &lock, u_short lock_id)
{
    this->release_area_lock(lock.pos);

    const int lock_index = cord(lock.pos.x_int(), lock.pos.y_int());
    const int limit = tile_lock_size(lock_id);
    const int height = static_cast<int>(this->blocks.size()) / 100;

    std::vector<bool> seen(this->blocks.size(), false);
    std::vector<int> queue{lock_index};
    seen[lock_index] = true;
    int claimed = 0;
    for (std::size_t head = 0; head < queue.size() && claimed < limit; ++head)
    {
        const int at = queue[head];
        const int ax = at % 100, ay = at / 100;
        const int next[4][2] = {{ax, ay - 1}, {ax - 1, ay}, {ax + 1, ay}, {ax, ay + 1}};
        for (const auto &n : next)
        {
            if (n[0] < 0 || n[0] >= 100 || n[1] < 0 || n[1] >= height) continue;
            const int i = cord(n[0], n[1]);
            if (seen[i]) continue;
            seen[i] = true;

            ::block &block = this->blocks[i];
            if (block.state[2] & S_PARENT) continue; // @note another lock's tile
            if (id_to_item(block.fg).type == type::LOCK) continue; // @note never lock a lock
            if (lock.ignore_air && block.fg == 0 && block.bg == 0) continue;
            if (claimed >= limit) break;

            block.state[2] |= S_PARENT;
            block.state[0] = static_cast<u_char>(lock_index & 0xff);
            block.state[1] = static_cast<u_char>((lock_index >> 8) & 0xff);
            ++claimed;
            queue.push_back(i);
        }
    }
}

bool world::can_edit(const ::peer &peer, const ::pos &pos, bool breaking)
{
    if (peer.role >= MODERATOR) return true;
    if (peer.owns(this->owner)) return true;

    if (const ::area_lock *lock = this->lock_at(pos))
    {
        if (peer.owns(lock->owner) || lock->is_public) return true;
        if (std::ranges::find(lock->access, peer.user_id) == lock->access.end()) return false;
        if (lock->builder_mode == 1 && breaking) return false;  // @note Builder's Lock: only building
        if (lock->builder_mode == 2 && !breaking) return false; // @note Builder's Lock: only breaking
        return true;
    }
    if (this->owner == 0 || this->is_public) return true;
    return std::ranges::find(this->access, peer.user_id) != this->access.end();
}

bool world::jammed() const
{
    return std::ranges::any_of(this->blocks, [](const ::block &block) { return block.fg == 226/*Signal Jammer*/ && (block.state[2] & S_TOGGLE); });
}

std::vector<world> worlds;

void send_gameMessage(ENetPeer *peer, ::gameMessage gameMessage) 
{
    send_data(*peer, compressMessage(gameMessage));
}

void send_data(ENetPeer &peer, const ::blob &blob)
{
    ENetPacket *packet = enet_packet_create(blob.data().data(), blob.size(), ENET_PACKET_FLAG_RELIABLE);
    if (packet == nullptr) return;

    if (enet_peer_send(&peer, 0, packet)) enet_packet_destroy(packet);
}

void send_gamePacket(ENetPeer &peer, ::gamePacket &gamePacket) 
{
    ::peer *pPeer = static_cast<::peer*>(peer.data);

    peers(pPeer->recent_worlds.back(), PeerSearch::WORLD, [&](ENetPeer *peer) 
    {
        send_data(*peer, compress_state(gamePacket));
    });
}

void tile_apply_damage(ENetEvent &event, ::gamePacket gamePacket, block &block, u_int value)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    (block.fg == 0) ? ++block.hits[1] : ++block.hits[0];
    gamePacket.type = (value << 24) | 0x000008; // @note 0x{}000008
    gamePacket.id = 6; // @note idk exactly
    gamePacket.netid = pPeer->netid;
	send_gamePacket(*event.peer, gamePacket);
}

u_short modify_item_inventory(ENetEvent &event, ::slot slot)
{   
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    ::gamePacket gamePacket{.id = slot.id};
    if (slot.count < 0) gamePacket.type = (slot.count*-1 << 16) | 0x000d; // @noote 0x00{}000d
    else                gamePacket.type = (slot.count    << 24) | 0x000d; // @noote 0x{}00000d
    send_data(*event.peer, compress_state(gamePacket)); // @note only the player whose backpack changes

    return pPeer->emplace(::slot(slot.id, slot.count));
}

void item_change_object(ENetEvent &event, ::gamePacket gamePacket) 
{
    gamePacket.type = 0x0e; // @note PACKET_ITEM_CHANGE_OBJECT

    send_gamePacket(*event.peer, gamePacket);
}

void merge_object(ENetEvent &event, ::slot slot, const ::pos &pos, ::world &world)
{
    auto object = std::ranges::find_if(world.objects, [&](const ::object &object) {
        return object.id == slot.id && (object.pos.by_32(true) == pos.by_32(true));
    });
    /* @todo avoid surpassing 200 and call add_object() for the remaining amount */ // @note future self reference peer::emplace()...
    object->count += slot.count;

    item_change_object(event, ::gamePacket{
        .netid = (int)0xfffffffd,
        .uid   = (int)object->uid,
        .count = static_cast<float>(object->count),
        .id    = object->id,
        .pos   = object->pos
    });
}

void remove_object(ENetEvent& event, signed uid)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    item_change_object(event, ::gamePacket{
        .netid = pPeer->netid,
        .uid   = (int)0xffffffff,
        .id    = uid
    });
}

int add_object(ENetEvent& event, ::slot slot, const ::pos& pos, ::world &world)
{
    /* @todo got a little messy */
    auto object = std::ranges::find_if(world.objects, [&](const ::object &object) {
        return object.id == slot.id && (object.pos.by_32(true) == pos.by_32(true));
    });
    if (object != world.objects.end() && object->count < 200/*@todo*/) 
    {
        merge_object(event, slot, pos, world);
        return object->uid;
    }
    ::object it = world.objects.emplace_back(::object(slot.id, slot.count, pos, ++world.last_object_uid)); // @note a iterator ahead of time

    item_change_object(event, ::gamePacket{
        .netid = (int)0xffffffff,
        .uid   = (int)it.uid,
        .count = static_cast<float>(slot.count),
        .id    = it.id,
        .pos   = pos
    });
    return it.uid;
}

void add_drop(ENetEvent &event, ::slot im, ::pos pos, ::world &world) // @todo
{
    add_object(event, im, ::pos{
        pos.x + RandomRange(0, 16),
        pos.y + RandomRange(0, 16)
    }, world);
}

int magplant_limit(short machine)
{
    return (machine == 6946/*Gaia's Beacon*/ || machine == 6948/*Unstable Tesseract*/) ? 1500 : 5000; // @note source: github.com/cppdecl/Nomikai Src/World/Tile.cc
}

void add_break_drop(ENetEvent &event, ::slot im, ::pos pos, ::world &world)
{
    if (im.id != 112/*Gems*/ && im.count > 0)
    {
        auto mag = std::ranges::find_if(world.magplants, [&](const ::magplant &m) {
            return m.item == im.id && m.collect && valid_tile(world, m.pos) &&
                   id_to_item(world.blocks[cord(m.pos.x_int(), m.pos.y_int())].fg).type == type::MAGPLANT &&
                   m.count + im.count <= magplant_limit(world.blocks[cord(m.pos.x_int(), m.pos.y_int())].fg);
        });
        if (mag != world.magplants.end())
        {
            mag->count += im.count;
            /* @note the item flies into the machine (PACKET_ITEM_EFFECT), like real Growtopia */
            ::gamePacket effect{
                .type  = 0x13,
                .uid   = -1,
                .delay = 200,
                .pos   = ::pos{mag->pos.x * 32.0f + 16.0f, mag->pos.y * 32.0f + 16.0f},
                .speed = ::pos{pos.x + 8.0f, pos.y + 8.0f},
                .punch = ::pos{static_cast<int>(im.id), static_cast<int>(im.id)}
            };
            ::blob blob = compress_state(effect);
            peers(world.name, PeerSearch::WORLD, [&blob](ENetPeer *peer) { send_data(*peer, blob); });
            return;
        }
    }
    add_drop(event, im, pos, world);
}

void send_tile_update(ENetEvent &event, ::gamePacket gamePacket, ::block &block, ::world &world) 
{
    gamePacket.type = 05; // @note PACKET_SEND_TILE_UPDATE_DATA
    gamePacket.state = (int)gamePacketState::EXTENDED;
    ::blob blob = compress_state(gamePacket);

    const int index = static_cast<int>(&block - world.blocks.data());
    if (index >= 0 && index < static_cast<int>(world.blocks.size()))
        blob.push_back(world.tile_blob(index));
    else return;

    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    peers(pPeer->recent_worlds.back(), PeerSearch::WORLD, [&blob](ENetPeer *peer) 
    {
        send_data(*peer, blob);
    });
}

void world_tile_update(::world &world, const ::pos &pos)
{
    if (!valid_tile(world, pos)) return;
    const int index = cord(pos.x_int(), pos.y_int());
    ::gamePacket gamePacket{ .type = 05, .state = (int)gamePacketState::EXTENDED, .id = world.blocks[index].fg, .punch = pos };
    ::blob blob = compress_state(gamePacket);
    blob.push_back(world.tile_blob(index));
    peers(world.name, PeerSearch::WORLD, [&blob](ENetPeer *peer) { send_data(*peer, blob); });
}

void send_particle_effect(ENetEvent &event, const ::pos& pos, ::pos speed, int id, float offset)
{
    ::gamePacket gamePacket{
        .type = 0x11, // @note PACKET_SEND_PARTICLE_EFFECT
        .particle_effect = id,
        .id = id,
        .pos = pos,
        .speed = speed,
        .idk = offset
    };
    send_gamePacket(*event.peer, gamePacket);
}

void remove_fire(ENetEvent &event, gamePacket gamePacket, ::block &block, ::world &world)
{
    send_particle_effect(event, gamePacket.punch.by_32(), {0x00, 0x95});

    block.state[3] &= ~S_FIRE;
    send_tile_update(event, gamePacket, block, world);

    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    if (++pPeer->fires_removed % 100 == 0) 
    {
        on::ConsoleMessage(event.peer, "`oI'm so good at fighting fires, I rescused this `2Highly Combustible Box``!");
        modify_item_inventory(event, {3090/*Combustible Box*/, 1});
    }
    pPeer->add_xp(event, 1);
}

void fireworks(ENetEvent &event, const ::pos &pos)
{
    int type  [3]{ RandomRange(0x25, 0x28), RandomRange(0x25, 0x28), RandomRange(0x25, 0x28) };
    int offset[3]{ RandomRange(260, 2200), RandomRange(260, 2200), RandomRange(260, 2200) };

    send_particle_effect(event, pos, {0xb3, type[0]}, 0xc8*0, offset[0]);
    send_particle_effect(event, pos, {0xbe, type[1]}, 0xc8*1, offset[1]);
    send_particle_effect(event, pos, {0x7c, type[2]}, 0xc8*2, offset[2]);
}

void generate_world(::world &world)
{
    u_short main_door = RandomRange(2, cord(0, 60) / 100 - 4);
    std::vector<::block> blocks(cord(0, 60), ::block{0, 0});
    const int x = blocks.size() / 60;
    
    for (int i = 0ull; i < blocks.size(); ++i)
    {
        ::block &block = blocks[i];
        if (i >= cord(0, 37))
        {
            block.bg = 14; // @note cave background
            if (i >= cord(0, 38) && i < cord(0, 50) /* (above) lava level */ && RandomRange(0, 38) <= 1) block.fg = 10; // rock
            else if (i > cord(0, 50) && i < cord(0, 54) /* (above) bedrock level */ && RandomRange(0, 8) < 3) block.fg = 4; // lava
            else block.fg = (i >= cord(0, 54)) ? 8 : 2;
        }
        if (i == cord(main_door, 36))
        {
            block.fg = 6;
            world.doors.emplace_back(::door("EXIT","","", ::pos(i % x, i / x))); // @todo seems a bit hardcoded.
        }
        else if (i == cord(main_door, 37)) block.fg = 8; // @note bedrock (below main door)
    }
    world.blocks = std::move(blocks);
}

bool door_mover(::world &world, const ::pos &pos)
{
    std::vector<::block> &blocks = world.blocks;

    if (!valid_tile(world, pos) || !valid_tile(world, ::pos{pos.x, pos.y + 1})) return false; // @note door needs a tile below it

    if (blocks[cord(pos.x, pos.y)].fg != 0 ||
        blocks[cord(pos.x, (pos.y + 1))].fg != 0) return false;

    for (std::size_t i = 0ull; i < blocks.size(); ++i)
    {
        if (blocks[i].fg == 6/*Main Door*/)
        {
            blocks[i].fg = 0; // @note remove main door
            if (i + 100ull < blocks.size()) blocks[i + 100ull].fg = 0; // @note remove bedrock below
            break;
        }
    }
    blocks[cord(pos.x, pos.y)].fg = 6;
    blocks[cord(pos.x, (pos.y + 1))].fg = 8;
    return true;
}

void blast::thermonuclear(::world &world)
{
    for (::block &block : world.blocks)
    {
        if (block.fg == 6/*main door*/ || block.fg == 8/*bedrock*/) continue;

        block.reset();
    }
}

namespace
{
    /* @note ground starts at row 37 (main door row 36), lava 50-53, bedrock 54+ (see generate_world) */
    constexpr int GROUND = 37, BEDROCK = 54;

    bool keep(short fg) { return fg == 6/*main door*/ || fg == 8/*bedrock*/; }

    /* @brief swap the dirt (and optionally the cave background) for a theme's blocks; top rows get `top` */
    void reskin(::world &world, short top, int top_rows, short fill, short bg)
    {
        for (int i = cord(0, GROUND); i < cord(0, BEDROCK); ++i)
        {
            ::block &block = world.blocks[i];
            if (keep(block.fg)) continue;
            if (block.fg == 2/*dirt*/) block.fg = (i < cord(0, GROUND + top_rows)) ? top : fill;
            if (bg != 0 && block.bg == 14/*cave background*/) block.bg = bg;
        }
    }
    int main_door_x(const ::world &world)
    {
        for (int x = 0; x < 100; ++x) if (world.blocks[cord(x, GROUND - 1)].fg == 6) return x;
        return -1;
    }
    /* @brief something standing on the ground (palm trees, cacti...) on random free surface tiles, away from the main door */
    void decorate(::world &world, short id, int count)
    {
        const int door = main_door_x(world);
        for (int tries = 0; count > 0 && tries < 400; ++tries)
        {
            const int x = RandomRange(1, 99);
            if (std::abs(x - door) < 3) continue;
            ::block &spot = world.blocks[cord(x, GROUND - 1)];
            if (spot.fg != 0 || world.blocks[cord(x, GROUND)].fg == 0) continue;
            spot.fg = id, --count;
        }
    }
    /* @brief a stepped pyramid (Desert Blast's pyramid, Jungle Blast's ziggurat) standing on the ground */
    void pyramid(::world &world, short block, int width)
    {
        const int door = main_door_x(world);
        int left = (door < 50) ? 60 : 10; // @note on the other side of the world from the main door
        for (int row = 0; width - row * 2 > 0; ++row)
            for (int x = left + row; x < left + width - row; ++x)
            {
                ::block &b = world.blocks[cord(x, GROUND - 1 - row)];
                if (b.fg == 0) b.fg = block;
            }
    }
    /* @brief random common trees, all ready to harvest (Harvest Moon Blast) */
    void grown_trees(::world &world)
    {
        std::vector<short> seeds{};
        for (short id = 3; id < 4000 && seeds.size() < 60; id += 2)
        {
            const ::item &seed = id_to_item(id), &block = id_to_item(static_cast<short>(id - 1));
            if (seed.type == type::SEED && block.type == type::FOREGROUND && block.rarity > 0 && block.rarity <= 30) seeds.push_back(id);
        }
        if (seeds.empty()) seeds.push_back(3);
        const int door = main_door_x(world);
        for (int x = 0; x < 100; ++x)
        {
            if (std::abs(x - door) < 2 || x % 2 == 1) continue; // @note every other tile, so they can be walked between
            ::block &spot = world.blocks[cord(x, GROUND - 1)];
            if (spot.fg != 0 || world.blocks[cord(x, GROUND)].fg == 0) continue;
            const short seed = seeds[RandomRange(0, static_cast<int>(seeds.size()))];
            spot.fg = seed;
            world.trees.emplace_back(::tree(ticks() - static_cast<u_int>(id_to_item(seed).tick), static_cast<u_char>(RandomRange(1, 5)), ::pos{x, GROUND - 1}));
        }
    }
}

bool blast::is_world_blast(int id)
{
    switch (id)
    {
        case 1402: case 830: case 942: case 1060: case 1136: case 1532: case 3562: case 4774:
        case 7380: case 7588: case 8556: case 8738: case 14896:
            return true;
    }
    return false;
}

bool blast::themed(::world &world, int blast)
{
    switch (blast)
    {
        case 830: // @note Beach Blast: sand and sea
        {
            reskin(world, 442/*Sand*/, 4, 2, 0);
            const int door = main_door_x(world);
            const int sea_from = (door < 50) ? 65 : 0, sea_to = (door < 50) ? 100 : 35;
            for (int x = sea_from; x < sea_to; ++x) // @note the sea: the shore dips 3 tiles and fills with water
                for (int y = GROUND - 1; y < GROUND + 3; ++y)
                {
                    ::block &b = world.blocks[cord(x, y)];
                    if (keep(b.fg)) continue;
                    if (y >= GROUND) b.fg = 0;
                    b.state[3] |= S_WATER;
                }
            for (int x = sea_from; x < sea_to; ++x) world.blocks[cord(x, GROUND + 3)].fg = 442;
            decorate(world, 854/*Palm Tree*/, 5);
            decorate(world, 848/*Beach Umbrella*/, 3);
            world.base_weather = 1;
            return true;
        }
        case 942: // @note Desert Blast: sand, sandstone, cacti and one free pyramid
        {
            reskin(world, 442/*Sand*/, 3, 332/*Sandstone*/, 434/*Sandstone Wall*/);
            pyramid(world, 332/*Sandstone*/, 21);
            decorate(world, 330/*Cactus*/, 6);
            world.base_weather = 3;
            return true;
        }
        case 1060: // @note Harvest Moon Blast: trees ready to harvest
        {
            grown_trees(world);
            world.base_weather = 6;
            return true;
        }
        case 1136: // @note Mars Blast
        {
            reskin(world, 1134/*Mars Rock*/, 0, 1134, 0);
            world.base_weather = 7;
            return true;
        }
        case 1532: // @note Undersea Blast: water everywhere above the sea floor
        {
            reskin(world, 1536/*Deep Sand*/, 3, 2, 0);
            for (int i = 0; i < cord(0, GROUND); ++i) world.blocks[i].state[3] |= S_WATER;
            decorate(world, 832/*Coral*/, 8);
            decorate(world, 846/*Seaweed*/, 8);
            world.base_weather = 14;
            return true;
        }
        case 3562: // @note Cave Blast: solid cave dirt with tunnels, crystals and darkness
        {
            for (int i = cord(0, 4); i < cord(0, BEDROCK); ++i)
            {
                ::block &b = world.blocks[i];
                if (keep(b.fg)) continue;
                if (b.fg == 0 || b.fg == 2) b.fg = 3564/*Cave Dirt*/;
                b.bg = 3556/*Dark Cave Background*/;
            }
            const int door = main_door_x(world);
            for (int dx = -2; dx <= 2; ++dx) // @note a pocket of air around the main door
                for (int y = GROUND - 4; y < GROUND; ++y)
                    if (door + dx >= 0 && door + dx < 100 && world.blocks[cord(door + dx, y)].fg == 3564) world.blocks[cord(door + dx, y)].fg = 0;
            for (int tunnel = 0; tunnel < 7; ++tunnel) // @note winding tunnels
            {
                int x = (tunnel == 0) ? door : RandomRange(2, 98), y = (tunnel == 0) ? GROUND - 2 : RandomRange(8, 48);
                for (int step = 0; step < 90; ++step)
                {
                    for (int dy = 0; dy < 2; ++dy)
                        if (x >= 0 && x < 100 && y + dy >= 4 && y + dy < 50 && world.blocks[cord(x, y + dy)].fg == 3564) world.blocks[cord(x, y + dy)].fg = 0;
                    x = std::clamp(x + RandomRange(-1, 2), 0, 99);
                    y = std::clamp(y + RandomRange(-1, 2), 5, 48);
                }
            }
            for (int n = 0; n < 25; ++n) // @note crystals on tunnel floors
            {
                const int x = RandomRange(0, 100), y = RandomRange(6, 49);
                if (world.blocks[cord(x, y)].fg == 0 && world.blocks[cord(x, y + 1)].fg == 3564)
                    world.blocks[cord(x, y)].fg = static_cast<short>(3580 + 2 * RandomRange(0, 3));
            }
            return true;
        }
        case 4774: // @note Jungle Blast: ferns and one free ziggurat
        {
            pyramid(world, 4796/*Jungle Pillar*/, 17);
            decorate(world, 4782/*Jungle Fern*/, 12);
            world.base_weather = 32;
            return true;
        }
        case 7380: // @note Monochrome Blast: blacks and whites only
        {
            reskin(world, 168/*White Block*/, 1, 166/*Black Block*/, 0);
            for (int i = cord(0, GROUND); i < cord(0, BEDROCK); ++i)
            {
                ::block &b = world.blocks[i];
                if (b.fg == 10/*rock*/ || b.fg == 4/*lava*/) b.fg = 168;
                if (b.bg == 14) b.bg = 1158/*Black Wallpaper*/;
            }
            world.base_weather = 43;
            return true;
        }
        case 7588: world.base_weather = 44; return true; // @note Treasure Blast
        case 8556: world.base_weather = 45; return true; // @note SurgWorld Blast
        case 8738: world.base_weather = 46; return true; // @note Bountiful Blast
        case 14896: // @note Candyland Blast
        {
            reskin(world, 1358/*Candy Cane Block*/, 2, 2, 0);
            world.base_weather = 78;
            return true;
        }
    }
    return false;
}
