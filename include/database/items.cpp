#include "pch.hpp"

#include "tools/terminal.hpp"
#include "database/database.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>

#include "items.hpp"
#include "commands/punch.hpp"
#include "database/playmods.hpp"

std::vector<::item> items;

const ::item &id_to_item(u_short id) noexcept // @note std::out_of_range is handled
{
    if (id >= items.size()) return items[0]; // @note we can't return a contructor so this is gonna be our dummy value
    
    return items[id];
}


std::vector<u_char> im_data(sizeof(::gamePacket)/*inital packet*/, 0x00);
u_int items_hash{};

namespace
{
    template<typename T>
    void shift_pos(const std::vector<u_char> &data, u_int &pos, T &value) noexcept // @note std::out_of_range is handled
    {
        u_char *i8 = reinterpret_cast<u_char*>(&value);

        if (pos + sizeof(T) > data.size())
        {
            pos = static_cast<u_int>(data.size());
            return;
        }
        for (std::size_t i = 0ull; i < sizeof(T); ++i)
        {
            i8[i] = data[pos + i];
        }
        pos += sizeof(T);
    }

    template<typename T>
    void data_modify(std::vector<u_char> &data, const u_int &pos, const T &value) noexcept
    {
        const u_char *i8 = reinterpret_cast<const u_char*>(&value);

        if (pos + sizeof(T) > data.size()) return;
        for (std::size_t i = 0ull; i < sizeof(T); ++i)
        {
            data[pos + i] = i8[i];
        }
    }

    short read_len(const std::vector<u_char> &data, u_int pos)
    {
        if (pos + sizeof(short) > data.size()) return 0;
        short len{};
        std::memcpy(&len, &data[pos], sizeof(short));
        return len;
    }

    const std::string_view token{"PBG892FXX982ABC*"};

    /* @note where the fields the website can change sit in one item's record (from the record's first byte) */
    struct fields
    {
        u_int name{}, texture{}, tex_x{}, tex_y{}, collision{}, hits{}, hit_reset{}, rarity{}, cat{}, info{};
        u_int type{}, hit_sound{}, spread{}, cloth{}, max_amount{}, extra_file{};
        u_int seed_base{}, seed_overlay{}, tree_base{}, tree_leaves{}, seed_color{}, seed_overlay_color{}, grow{};
        bool has_info{};
    };

    u_short version{};
    std::vector<std::vector<u_char>> originals{}; // @note each item's record exactly as items.dat has it
    std::vector<std::vector<u_char>> records{};   // @note with this server's edits and custom items

    /*
     * @brief reads one item's record
     * @return the position after it, or 0 when items.dat ended early
     */
    u_int parse(std::vector<u_char> &data, u_int pos, ::item &item, fields &at)
    {
        const u_int start = pos;
        if (pos + 8 > data.size()) return 0;

        shift_pos(data, pos, item.id); pos += 2; // @note downside im.id to 2 bit (short)
        shift_pos(data, pos, item.property);

        at.cat = pos - start;
        shift_pos(data, pos, item.cat);

        at.type = pos - start;
        shift_pos(data, pos, item.type);
        at.hit_sound = pos - start;
        shift_pos(data, pos, item.hit_sound);

        at.name = pos - start;
        short len = read_len(data, pos);
        pos += sizeof(short);
        if (len < 0 || pos + len > data.size()) return 0;
        item.raw_name.resize(len);
        for (short i = 0; i < len; ++i)
            item.raw_name[i] = data[pos] ^ token[(i + item.id) % token.length()],
            ++pos;

        at.texture = pos - start;
        len = read_len(data, pos);
        pos += sizeof(short);
        if (len < 0 || pos + len > data.size()) return 0;
        item.texture.assign(reinterpret_cast<char*>(&data[pos]), len);
        pos += len;

        pos += sizeof(int);
        pos += sizeof(u_char);

        shift_pos(data, pos, item.ingredient);
        at.tex_x = pos - start;
        shift_pos(data, pos, item.tex_x);
        at.tex_y = pos - start;
        shift_pos(data, pos, item.tex_y);
        at.spread = pos - start;
        shift_pos(data, pos, item.spread);
        pos += sizeof(u_char);

        at.collision = pos - start;
        shift_pos(data, pos, item.collision);
        at.hits = pos - start;
        shift_pos(data, pos, item.hits);
        if (item.hits != 0) item.hits /= 6; // @note unknown reason behind why break hit is muliplied by 6 then having to divide by 6

        at.hit_reset = pos - start;
        shift_pos(data, pos, item.hit_reset);

        at.cloth = pos - start;
        if (item.type == type::CLOTHING) shift_pos(data, pos, item.cloth_type);
        else pos += 1; // @note assign nothing
        if (item.type == type::AURA) item.cloth_type = clothing::ANCES;
        at.rarity = pos - start;
        shift_pos(data, pos, item.rarity);

        at.max_amount = pos - start;
        shift_pos(data, pos, item.max_amount);
        {
            at.extra_file = pos - start;
            len = read_len(data, pos);
            pos += sizeof(short);
            if (len < 0 || pos + len > data.size()) return 0;
            std::string audio_directory(reinterpret_cast<char*>(&data[pos]), len);
            item.extra_file = audio_directory;
            pos += len;

            if (audio_directory.ends_with(".mp3"))
                data_modify(data, pos, 0); // @todo make it only for IOS
        }
        pos += sizeof(int);

        pos += sizeof(std::array<u_char, 4ull>);

        for (int i = 0; i < 4; ++i) pos += read_len(data, pos) + sizeof(short);

        at.seed_base = pos - start;
        shift_pos(data, pos, item.seed_base);
        at.seed_overlay = pos - start;
        shift_pos(data, pos, item.seed_overlay);
        at.tree_base = pos - start;
        shift_pos(data, pos, item.tree_base);
        at.tree_leaves = pos - start;
        shift_pos(data, pos, item.tree_leaves);
        at.seed_color = pos - start;
        shift_pos(data, pos, item.seed_color);
        at.seed_overlay_color = pos - start;
        shift_pos(data, pos, item.seed_overlay_color);
        pos += sizeof(int); // @note ingredient (unused)

        at.grow = pos - start;
        shift_pos(data, pos, item.tick);

        pos += sizeof(short);
        pos += sizeof(short);

        for (int i = 0; i < 3; ++i) pos += read_len(data, pos) + sizeof(short);

        pos += sizeof(std::array<u_char, 80ull>);

        if (version >= 0x0b) pos += read_len(data, pos) + sizeof(short); // @date February 2019
        if (version >= 0x0c) // @date October 2020
        {
            pos += sizeof(int);
            pos += sizeof(std::array<u_char, 9ull>);
        }
        if (version >= 0x0d) pos += sizeof(int); // @date May 2021
        if (version >= 0x0e) pos += sizeof(int); // @date October 2021
        if (version >= 0x0f)
        {
            pos += sizeof(std::array<u_char, 25ull>);
            pos += read_len(data, pos) + sizeof(short);
        }
        if (version >= 0x10) pos += read_len(data, pos) + sizeof(short);
        if (version >= 0x11) pos += sizeof(int); // @date April 2024
        if (version >= 0x12) pos += sizeof(int); // @date December 2024
        if (version >= 0x13) pos += sizeof(std::array<u_char, 9ull>);
        if (version >= 0x15) pos += sizeof(short); // @date September 2025
        if (version >= 0x16)
        {
            at.info = pos - start;
            at.has_info = true;
            len = read_len(data, pos);
            pos += sizeof(short);
            if (len < 0 || pos + len > data.size()) return 0;
            item.info.assign(reinterpret_cast<char*>(&data[pos]), len);
            pos += len;
        }
        if (version >= 0x17)
        {
            shift_pos(data, pos, item.splice[0]);
            shift_pos(data, pos, item.splice[1]);
        }
        if (version >= 0x18) pos += sizeof(u_char); // @date December 2025
        if (version >= 0x19)
        {
            len = read_len(data, pos);
            pos += sizeof(short);
            if (len > (sizeof(short) + sizeof(int))) // @note {size} 0x00 0x00 0x00 0x00
            {
                // @todo add the string for the player punch FX
                pos += len;
            }
            pos += sizeof(int); // @note default: 0x00 0x00 0x00 0x00
        }
        if (version >= 0x1a) pos += sizeof(u_char); // May 2026
        if (version == 0x1b) // September 2026 | Immortal items
        {
            len = read_len(data, pos);
            pos += sizeof(short);
            if (len > 0) pos += sizeof(u_char); /* 0x36 */
        }
        return pos > data.size() ? 0 : pos;
    }

    bool parse_record(std::vector<u_char> &record, ::item &item, fields &at)
    {
        return parse(record, 0, item, at) == record.size();
    }

    void set_string(std::vector<u_char> &record, u_int at, const std::string &text)
    {
        const short old = read_len(record, at);
        const short len = static_cast<short>(std::min<std::size_t>(text.size(), 0x7fff));
        std::vector<u_char> bytes(sizeof(short) + len);
        std::memcpy(bytes.data(), &len, sizeof(short));
        std::memcpy(bytes.data() + sizeof(short), text.data(), len);
        record.erase(record.begin() + at, record.begin() + at + sizeof(short) + old);
        record.insert(record.begin() + at, bytes.begin(), bytes.end());
    }

    void set_name(std::vector<u_char> &record, const std::string &name)
    {
        ::item item{}; fields at{};
        parse_record(record, item, at);
        std::string coded = name;
        for (std::size_t i = 0; i < coded.size(); ++i) coded[i] = static_cast<char>(coded[i] ^ token[(i + item.id) % token.length()]);
        set_string(record, at.name, coded);
    }

    /* @note a copy of another record under a new id. the name is coded with the id, so it's written again */
    std::vector<u_char> clone(const std::vector<u_char> &from, int id)
    {
        std::vector<u_char> record = from;
        ::item item{}; fields at{};
        parse_record(record, item, at);
        data_modify(record, 0, id);
        set_name(record, item.raw_name);
        return record;
    }

    /* @brief one row of item_edits. a missing value is left as items.dat has it */
    struct edit
    {
        int id{}, base{-1};
        bool removed{};
        std::optional<std::string> name, texture, info, extra_file;
        std::optional<int> rarity, hits, hit_reset, collision, cat, tex_x, tex_y;
        std::optional<int> type, hit_sound, cloth, spread, max_amount, grow, seed_base, seed_overlay, tree_base, tree_leaves;
        std::optional<long long> seed_color, seed_overlay_color;
        std::optional<int> punch, powers; // @note not in items.dat: the server's punch effect and powers when worn
    };

    void apply(std::vector<u_char> &record, const edit &e)
    {
        ::item item{}; fields at{};
        if (!parse_record(record, item, at)) return;
        if (e.tex_x) data_modify(record, at.tex_x, static_cast<u_char>(std::clamp(*e.tex_x, 0, 255)));
        if (e.tex_y) data_modify(record, at.tex_y, static_cast<u_char>(std::clamp(*e.tex_y, 0, 255)));
        if (e.collision) data_modify(record, at.collision, static_cast<u_char>(std::clamp(*e.collision, 0, 12)));
        if (e.hits) data_modify(record, at.hits, static_cast<u_char>(std::clamp(*e.hits, 0, 42) * 6));
        if (e.hit_reset) data_modify(record, at.hit_reset, std::clamp(*e.hit_reset, 0, 86400));
        if (e.rarity) data_modify(record, at.rarity, static_cast<short>(std::clamp(*e.rarity, 0, 999)));
        if (e.cat) data_modify(record, at.cat, static_cast<u_char>(std::clamp(*e.cat, 0, 255)));
        const auto byte = [&](const std::optional<int> &v, u_int where) { if (v) data_modify(record, where, static_cast<u_char>(std::clamp(*v, 0, 255))); };
        byte(e.type, at.type); byte(e.hit_sound, at.hit_sound); byte(e.cloth, at.cloth); byte(e.spread, at.spread);
        byte(e.max_amount, at.max_amount);
        byte(e.seed_base, at.seed_base); byte(e.seed_overlay, at.seed_overlay); byte(e.tree_base, at.tree_base); byte(e.tree_leaves, at.tree_leaves);
        if (e.seed_color) data_modify(record, at.seed_color, static_cast<u_int>(std::clamp<long long>(*e.seed_color, 0, 0xffffffffLL)));
        if (e.seed_overlay_color) data_modify(record, at.seed_overlay_color, static_cast<u_int>(std::clamp<long long>(*e.seed_overlay_color, 0, 0xffffffffLL)));
        if (e.grow) data_modify(record, at.grow, std::clamp(*e.grow, 0, 31536000));
        /* @note strings last, latest in the record first, so the places read above don't move */
        if (e.info && at.has_info) set_string(record, at.info, *e.info);
        if (e.extra_file) set_string(record, at.extra_file, *e.extra_file);
        if (e.texture) set_string(record, at.texture, *e.texture);
        if (e.name) set_name(record, *e.name);
    }

    std::vector<edit> load_edits()
    {
        std::vector<edit> out{};
        if (db.mysql == nullptr || mysql_query(db.mysql,
            "SELECT id, base_id, removed, name, texture, info, rarity, hits, hit_reset, collision, cat, tex_x, tex_y, "
            "type, hit_sound, cloth, spread, max_amount, grow, seed_base, seed_overlay, tree_base, tree_leaves, seed_color, seed_overlay_color, "
            "extra_file, punch, powers FROM item_edits ORDER BY id"))
            return out; // @note no table yet: nothing was changed on the website
        MYSQL_RES *res = mysql_store_result(db.mysql);
        if (res == nullptr) return out;
        const auto text = [](const char *v) { return v ? std::optional<std::string>(v) : std::nullopt; };
        const auto num = [](const char *v) { return v ? std::optional<int>(std::atoi(v)) : std::nullopt; };
        while (MYSQL_ROW row = mysql_fetch_row(res))
        {
            edit e{};
            e.id = row[0] ? std::atoi(row[0]) : -1;
            e.base = row[1] ? std::atoi(row[1]) : -1;
            e.removed = row[2] && row[2][0] == '1';
            e.name = text(row[3]); e.texture = text(row[4]); e.info = text(row[5]);
            e.rarity = num(row[6]); e.hits = num(row[7]); e.hit_reset = num(row[8]);
            e.collision = num(row[9]); e.cat = num(row[10]); e.tex_x = num(row[11]); e.tex_y = num(row[12]);
            e.type = num(row[13]); e.hit_sound = num(row[14]); e.cloth = num(row[15]); e.spread = num(row[16]); e.max_amount = num(row[17]);
            e.grow = num(row[18]); e.seed_base = num(row[19]); e.seed_overlay = num(row[20]); e.tree_base = num(row[21]); e.tree_leaves = num(row[22]);
            e.seed_color = row[23] ? std::optional<long long>(std::atoll(row[23])) : std::nullopt;
            e.seed_overlay_color = row[24] ? std::optional<long long>(std::atoll(row[24])) : std::nullopt;
            e.extra_file = text(row[25]); e.punch = num(row[26]); e.powers = num(row[27]);
            if (e.id >= 0 && e.id < 0xffff) out.push_back(e);
        }
        mysql_free_result(res);
        return out;
    }

    /* @note what the client checks its cached items.dat against */
    u_int proton_hash(const std::vector<u_char> &data, std::size_t from)
    {
        u_int acc = 0x55555555;
        for (std::size_t i = from; i < data.size(); ++i) acc = (acc >> 27) + (acc << 5) + data[i];
        return acc;
    }

    /* @note puts items.dat back together from the records, ready to send */
    void rebuild()
    {
        std::size_t size = sizeof(u_short) + sizeof(u_int);
        for (const auto &record : records) size += record.size();

        im_data = compress_state(::gamePacket{
            .type = 0x10,/*PACKET_SEND_ITEM_DATABASE_DATA*/
            .state = (int)gamePacketState::EXTENDED,
            .size = static_cast<u_int>(size)
        }).data();
        const std::size_t body = im_data.size();
        im_data.reserve(body + size);
        const u_int count = static_cast<u_int>(records.size());
        im_data.insert(im_data.end(), reinterpret_cast<const u_char*>(&version), reinterpret_cast<const u_char*>(&version) + sizeof(version));
        im_data.insert(im_data.end(), reinterpret_cast<const u_char*>(&count), reinterpret_cast<const u_char*>(&count) + sizeof(count));
        for (const auto &record : records) im_data.insert(im_data.end(), record.begin(), record.end());
        items_hash = proton_hash(im_data, body);

        std::vector<::item> fresh{};
        fresh.reserve(records.size());
        for (auto &record : records)
        {
            ::item item{}; fields at{};
            parse_record(record, item, at);
            fresh.emplace_back(std::move(item));
        }
        items = std::move(fresh);
    }
}

std::size_t items_original_count() { return originals.size(); }

int items_apply_edits()
{
    records = originals;
    const std::vector<edit> edits = load_edits();
    const int original = static_cast<int>(originals.size());

    int last = original - 1;
    for (const edit &e : edits) if (e.id >= original && !e.removed) last = std::max(last, e.id);
    /* @note custom items go after the game's own. a gap (or a removed custom item) is filled with a copy of Blank so ids never move */
    for (int id = original; id <= last; ++id)
    {
        auto it = std::ranges::find(edits, id, &edit::id);
        const bool usable = it != edits.end() && !it->removed && it->base >= 0 && it->base < original;
        std::vector<u_char> record = clone(originals[usable ? it->base : 0], id);
        if (!usable) set_name(record, "");
        records.emplace_back(std::move(record));
    }
    int applied = 0;
    for (const edit &e : edits)
    {
        if (e.id >= static_cast<int>(records.size()) || (e.removed && e.id >= original)) continue;
        if (e.base >= 0 && e.id < original) // @note a newer items.dat has a real item where this custom item was
        {
            print(std::format("custom item {} ({}) is hidden: items.dat now has its own item with that id", e.id, e.name.value_or("?")));
            continue;
        }
        apply(records[e.id], e);
        ++applied;
    }
    rebuild();

    /* @note punch effects and powers live in the server, not items.dat. a custom item keeps its original's unless the website set them */
    set_punch_overrides({});
    set_playmod_overrides({});
    std::unordered_map<u_short, u_char> punches{};
    std::unordered_map<u_short, std::pair<u_short, std::string>> powers{};
    for (const edit &e : edits)
    {
        if (e.id >= static_cast<int>(items.size()) || e.removed) continue;
        const bool custom = e.base >= 0 && e.id >= original;
        const u_short id = static_cast<u_short>(e.id);
        if (e.punch) punches[id] = static_cast<u_char>(std::clamp(*e.punch, 0, 255));
        else if (custom) { if (u_char p = get_punch_id(static_cast<u_int>(e.base)); p) punches[id] = p; }
        const ::playmod *base_mod = custom ? get_playmod(static_cast<u_short>(e.base)) : nullptr;
        if (e.powers) powers[id] = { static_cast<u_short>(*e.powers & 0xffff), base_mod ? base_mod->name : items[id].raw_name };
        else if (base_mod) powers[id] = { base_mod->mods, base_mod->name };
    }
    set_punch_overrides(std::move(punches));
    set_playmod_overrides(std::move(powers));
    return applied;
}

void decode_items_dat()
{
    if (!std::filesystem::exists("items.dat"))
        throw std::runtime_error("items.dat is missing. copy it from your Growtopia cache folder into the server folder.");

    const u_int size = std::filesystem::file_size("items.dat");
    std::vector<u_char> file(size);
    std::ifstream("items.dat", std::ios::binary).read(reinterpret_cast<char*>(file.data()), size);

    u_int pos = 0;
    shift_pos(file, pos, version);
    u_int count{};
    shift_pos(file, pos, count);

    if (version > 0x1b) print(std::format("items.dat version {} is newer than this server knows, it may not load correctly", version));

    originals.clear();
    originals.reserve(count);
    for (u_int i = 0; i < count; ++i)
    {
        ::item item{}; fields at{};
        const u_int end = (pos < file.size()) ? parse(file, pos, item, at) : 0;
        if (end == 0) throw std::runtime_error("items.dat ended early, this version is not supported.");
        originals.emplace_back(file.begin() + pos, file.begin() + end);
        pos = end;
    }
    const int edited = items_apply_edits();
    print(std::format("items.dat parsed successfully!{}", edited ? std::format(" ({} items changed or added on the website)", edited) : ""));
}
