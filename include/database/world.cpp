#include "pch.hpp"
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
            blob.u32(static_cast<u_int>(item.tick)); // @note always ready
            break;
        }
        case '\x12': /* xenonite crystal: u8 + u32 (gtps-guide tile-extras 0x12). the powers are applied by the server, so both stay 0 */
        {
            blob.u8(0);
            blob.u32(0);
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
    enum extra_kind : u_char { EX_DOOR = 1, EX_AREA_LOCK = 2, EX_VENDING = 3, EX_STORAGE = 4, EX_DISPLAY = 5, EX_XENONITE = 6 };
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
            case EX_XENONITE:
            {
                if (!blob.read_u32(this->xenonite, pos)) return;
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
