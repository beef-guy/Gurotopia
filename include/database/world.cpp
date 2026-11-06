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
        case type::MAIN_DOOR: case type::DOOR: case type::PORTAL: return '\x01';
        case type::SIGN: return '\x02';
        case type::LOCK: return '\x03';
        case type::SEED: return '\x04';
        case type::RANDOM: return '\x08';
        case type::PROVIDER: return '\x09';
        case type::DISPLAY_BLOCK: return '\x17';
        case type::VENDING_MACHINE: return '\x18';
        case type::STORAGE_BOX: return '\x36';
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
        case '\x03': /* lock: settings, owner, access count, access, minimum level, 7 unknown */
        {
            if (is_tile_lock(block.fg))
            {
                auto lock = std::ranges::find(this->area_locks, block_pos, &::area_lock::pos);
                if (lock != this->area_locks.end())
                {
                    blob.u8(lock->ignore_air ? 0x01 : 0x00);
                    blob.i32(lock->owner);
                    blob.i32(static_cast<int>(lock->access.size()));
                    for (const int uid : lock->access) blob.i32(uid);
                }
                else
                {
                    blob.u8(0x00);
                    blob.i32(0);
                    blob.i32(0);
                }
                blob.u8(0x00);
            }
            else
            {
                this->is_public = (block.state[2] & S_PUBLIC);
                blob.u8(this->lock_state);
                blob.i32(this->owner);
                blob.i32(static_cast<int>(std::ranges::count_if(this->access, [](int uid) { return uid > 0; })));
                for (const int uid : this->access) if (uid > 0) blob.i32(uid); // @note the access list follows its count
                blob.u8(this->minimum_entry_level);
            }
            for (int pad = 0; pad < 7; ++pad) blob.u8(0x00);
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
    enum extra_kind : u_char { EX_DOOR = 1, EX_AREA_LOCK = 2, EX_VENDING = 3, EX_STORAGE = 4, EX_DISPLAY = 5 };
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
                    if (lock.is_public) this->blocks[cord(x, y)].state[2] |= S_PUBLIC; // @note yellow border
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
