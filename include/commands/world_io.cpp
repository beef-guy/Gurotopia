#include "pch.hpp"

#include "action/carnival.hpp" // @note is_carnival_world()
#include "tools/string.hpp"
#include "tools/time.hpp"

#include "world_io.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace
{
    namespace fs = std::filesystem;
    const fs::path FOLDER = "worlds";

    void say(ENetEvent &event, const std::string &text)
    {
        send_gameMessage(event.peer, {.message = std::format("action|log\nmsg|{}", text)});
    }
    std::string upper(std::string text)
    {
        for (char &c : text) c = static_cast<char>(std::toupper(static_cast<u_char>(c)));
        return text;
    }

    /* ---------------- reading ---------------- */

    struct reader
    {
        const std::vector<u_char> &d;
        std::size_t p{};

        void need(std::size_t n) const { if (p + n > d.size()) throw std::runtime_error("the file ends too early"); }
        u_char u8() { need(1); return d[p++]; }
        u_short u16() { need(2); u_short v{}; std::memcpy(&v, d.data() + p, 2); p += 2; return v; }
        u_int u32() { need(4); u_int v{}; std::memcpy(&v, d.data() + p, 4); p += 4; return v; }
        void skip(std::size_t n) { need(n); p += n; }
        std::string str() { const u_short n = u16(); need(n); std::string s(d.begin() + p, d.begin() + p + n); p += n; return s; }
        void u32_list() { const u_int n = u32(); if (n > 100000) throw std::runtime_error("a list is too long"); skip(static_cast<std::size_t>(n) * 4); }
    };

    /* @brief what this server keeps from a tile */
    struct tile_in
    {
        short fg{}, bg{};
        u_short flags{};
        u_char type{};          // @note the extra's type, 0 = none
        std::string text{};     // @note door label, sign text, mannequin label, fish mount label
        u_int a{}, b{};         // @note tree/provider age, dice/team, display/vending/weather item, price/gravity, kranken colour...
        std::array<u_int, 9> list{};
    };

    struct world_in
    {
        std::string name{};
        int width{}, height{};
        int version{};
        u_short base_weather{};
        std::vector<tile_in> tiles{};
    };

    bool has_guild_trailer(const ::item &item) { return item.raw_name.starts_with("Guild "); } // @note items.dat flag GUILD_ITEM; this server doesn't read flags2

    /* @brief one tile extra, as a server sends it (gtps-guide guide/tile-extras.md) */
    void read_extra(reader &r, tile_in &t, int v)
    {
        const u_short fg = static_cast<u_short>(t.fg);
        switch (t.type)
        {
            case 0x01: t.text = r.str(); r.u8(); break;
            case 0x02: t.text = r.str(); r.u32(); break;
            case 0x03:
                t.a = r.u8(); r.u32(); r.u32_list();
                if (v >= 12) r.u32();
                if (v >= 13) r.u32();
                break;
            case 0x04: t.a = r.u32(); t.b = r.u8(); break;
            case 0x06: case 0x07: case 0x0c: case 0x0d: r.str(); r.str(); r.str(); r.u8(); break;
            case 0x08: case 0x10: t.a = r.u8(); break;
            case 0x09:
                t.a = r.u32();
                if (fg == 5318 || (fg == 10656 && v > 16)) r.u32();
                break;
            case 0x0a: r.u32(); r.u8(); break;
            case 0x0b: r.u32(); r.str(); break;
            case 0x0e:
            {
                t.text = r.str(); r.u8(); r.u32();
                bool special = false;
                for (int i = 0; i < 9; ++i) { t.list[i] = r.u16(); special |= (t.list[i] == 5712 || t.list[i] == 10044); }
                if (special && v >= 4) { r.u32(); r.u32(); }
                break;
            }
            case 0x0f: case 0x1b: case 0x26: case 0x27: case 0x29: case 0x32: case 0x39: case 0x49: case 0x4e: case 0x4f: r.u32(); break;
            case 0x12: case 0x1a: r.u8(); r.u32(); break;
            case 0x13: r.skip(18); break;
            case 0x14: case 0x36: case 0x40: r.str(); break;
            case 0x15: r.str(); r.u32(); r.u8(); break;
            case 0x17: t.a = r.u32(); break;
            case 0x18: t.a = r.u32(); t.b = r.u32(); break;
            case 0x19: r.u8(); r.u32_list(); break;
            case 0x1c: r.u8(); r.u32(); r.u8(); break;
            case 0x1e: r.u8(); r.u32(); break;
            case 0x1f:
                r.u8(); r.str(); r.u32(); r.u32(); r.u32(); r.u8();
                if (fg == 3316) r.skip(16);
                break;
            case 0x20: r.u32_list(); break;
            case 0x21: if (fg == 3394) r.str(); break;
            case 0x23: r.u32(); r.str(); break;
            case 0x24: r.str(); r.skip(12); break;
            case 0x25:
            {
                r.str();
                const u_int n = r.u32();
                r.u32();
                if (n > 100000) throw std::runtime_error("a list is too long");
                r.skip(static_cast<std::size_t>(n) * 4);
                break;
            }
            case 0x28: t.a = r.u32(); break;
            case 0x2a:
                r.u8();
                if (v > 18) r.skip(12);
                if (v > 19) r.skip(8);
                break;
            case 0x2b: for (int i = 0; i < 4; ++i) t.list[i] = r.u32(); break;
            case 0x2c: r.u8(); r.u32(); r.u32_list(); break;
            case 0x2f: t.text = r.str(); t.a = r.u32(); t.b = r.u8(); break;
            case 0x30:
            {
                r.str(); r.skip(16);
                const u_short face = r.u16(), hat = r.u16(), hair = r.u16();
                const auto special = [](u_short id) { return id == 5712 || id == 10044; };
                if (v > 3 && (special(face) || special(hat) || special(hair))) r.skip(8);
                if (v > 8) r.u32();
                if (v > 22 && hat == 12958) r.str();
                break;
            }
            case 0x31: t.a = r.u32(); t.b = r.u32(); r.u8(); break;
            case 0x35: r.skip(8); break;
            case 0x37: r.u32(); r.u32_list(); r.u32(); if (v > 13) r.skip(8); break;
            case 0x38: r.str(); r.u32(); break;
            case 0x3c: r.u32(); r.u8(); break;
            case 0x3d: r.u32(); r.u8(); r.u8(); r.skip(28); r.u8(); break;
            case 0x3e: r.skip(4 + 4 + 1 + 1 + 4); break;
            case 0x3f:
            {
                const u_int n = r.u32();
                if (n > 10000) throw std::runtime_error("a list is too long");
                for (u_int i = 0; i < n; ++i) { r.u32(); r.u8(); r.u32(); r.u32(); r.str(); }
                r.skip(8);
                break;
            }
            case 0x42: r.u8(); break;
            case 0x43: r.u32(); r.u32_list(); break;
            case 0x44: r.u32(); r.str(); r.str(); r.u32_list(); break;
            case 0x45: case 0x46: r.skip(12); if (v > 7) r.u32(); break;
            case 0x47: r.skip(12); if (v > 7) r.u32(); r.skip(4 + 4 + 1 + 1 + 4 + 4); break;
            case 0x48: r.skip(12); break;
            case 0x4b: r.u32(); r.str(); break;
            case 0x4c:
                for (int k = 0; k < 3; ++k)
                {
                    r.skip(100);
                    const u_int n = r.u32();
                    if (n > 100000) throw std::runtime_error("a list is too long");
                    r.skip(static_cast<std::size_t>(n) * 8);
                }
                break;
            case 0x4d: r.u32(); r.u32_list(); break;
            case 0x50: r.u32(); t.a = r.u32(); break;
            case 0x51: r.skip(8); break;
            case 0x05: case 0x11: case 0x16: case 0x1d: case 0x22: case 0x2d: case 0x2e: case 0x33: case 0x34:
            case 0x3a: case 0x3b: case 0x41: case 0x4a: case 0x52: break; // @note no bytes from a server
            default: throw std::runtime_error(std::format("unknown tile data type {}", static_cast<int>(t.type)));
        }

        /* @note the common trailer (gtps-guide guide/world-data.md) */
        const ::item &item = id_to_item(fg);
        if (has_guild_trailer(item))
        {
            if (fg != 5814/*Guild Lock*/) r.u8();
            r.skip(16);
        }
        else if (fg == 6546/*Data Starship Hull*/ && v >= 5)
        {
            r.skip(24);
            if (v >= 15) r.u32();
        }
    }

    /* @return the minimum world version a tile component is read from, -1 if the item has none (or a server never sends it) */
    int component_version(u_short fg)
    {
        if (fg == 3548) return 24;
        if (fg == 14662 || fg == 14666 || fg == 15376 || fg == 15546 || fg == 16094) return 0;
        if (fg >= 8624 && fg <= 8714 && (fg - 8624) % 6 == 0) return 25;
        return -1;
    }

    /* @brief the tiles after the header, from `start` */
    bool read_tiles(const std::vector<u_char> &data, std::size_t start, world_in &w)
    {
        reader r{ data, start };
        const std::size_t count = static_cast<std::size_t>(w.width) * w.height;
        w.tiles.assign(count, {});
        try
        {
            for (tile_in &t : w.tiles)
            {
                t.fg = static_cast<short>(r.u16());
                t.bg = static_cast<short>(r.u16());
                if (static_cast<u_short>(t.fg) >= items.size() || static_cast<u_short>(t.bg) >= items.size()) return false;
                r.u16(); // @note parent: this server works out area locks again
                t.flags = r.u16();
                if (t.flags & 0x02) r.u16();
                if (t.flags & 0x01)
                {
                    t.type = r.u8();
                    if (t.type == 0 || t.type > 0x52) return false;
                    read_extra(r, t, w.version);
                }
                if (w.version > 20)
                    if (const int min = component_version(static_cast<u_short>(t.fg)); min >= 0 && w.version >= min)
                        r.skip(r.u32());
            }
        }
        catch (const std::exception &) { return false; }

        /* @note the footer: 12 skipped bytes, the drops (not imported: they would be free items), then the weather */
        try
        {
            r.skip(12);
            const u_int drops = r.u32();
            r.u32();
            r.skip(static_cast<std::size_t>(drops) * 16);
            if (w.version >= 3) w.base_weather = r.u16();
        }
        catch (const std::exception &) {} // @note a world without a footer still imports
        return true;
    }

    /* @brief finds the header (some tools put a few bytes in front of it), then the tiles */
    world_in read_world(const std::vector<u_char> &data)
    {
        for (std::size_t o = 0; o + 2 < data.size() && o < 64; ++o)
        {
            const u_short len = static_cast<u_short>(data[o] | (data[o + 1] << 8));
            if (len == 0 || len > 24 || o + 2 + len + 12 > data.size()) continue;
            if (!std::all_of(data.begin() + o + 2, data.begin() + o + 2 + len, [](u_char c) { return std::isalnum(c); })) continue;

            world_in w{};
            w.name.assign(data.begin() + o + 2, data.begin() + o + 2 + len);
            std::size_t p = o + 2 + len;
            std::memcpy(&w.width, data.data() + p, 4);
            std::memcpy(&w.height, data.data() + p + 4, 4);
            if (w.width <= 0 || w.height <= 0 || w.width > 1000 || w.height > 1000) continue;
            p += 8;
            w.version = (o >= 6) ? (data[o - 6] | (data[o - 5] << 8)) : 0;
            if (w.version > 100) w.version = 0;

            /* @note a tile count, then (from a server) 5 skipped bytes. try that first, then the other likely layouts */
            for (std::size_t gap : { 4 + 5, 4, 2 + 7, 2, 0 })
                if (p + gap <= data.size() && read_tiles(data, p + gap, w)) return w;
        }
        throw std::runtime_error("this isn't a world file this server can read");
    }

    /* ---------------- building the world ---------------- */

    void apply(::world &world, const world_in &in, int owner)
    {
        world.blocks.assign(in.tiles.size(), ::block{0, 0});
        world.doors.clear(); world.signs.clear(); world.trees.clear(); world.random_blocks.clear(); world.displays.clear();
        world.area_locks.clear(); world.vendings.clear(); world.providers.clear(); world.shelves.clear(); world.mannequins.clear();
        world.weather_settings.clear(); world.fish_mounts.clear(); world.game_tiles.clear(); world.krankens.clear();
        world.weather = ::pos{-1, -1};
        world.base_weather = in.base_weather;

        const u_int now = ticks();
        bool world_lock = false;
        for (std::size_t i = 0; i < in.tiles.size(); ++i)
        {
            const tile_in &t = in.tiles[i];
            ::block &b = world.blocks[i];
            b.fg = t.fg, b.bg = t.bg;
            b.state[2] = static_cast<u_char>(t.flags & 0xe0); // @note flipped, on, public
            b.state[3] = static_cast<u_char>(t.flags >> 8);   // @note paint, glue, water, fire, background on, alt mode
            const ::pos pos{ static_cast<int>(i % 100), static_cast<int>(i / 100) };
            const ::item &item = id_to_item(static_cast<u_short>(t.fg));
            if (t.fg == 0) continue;

            switch (t.type)
            {
                case 0x01: world.doors.emplace_back(t.text, "", "", pos); break;
                case 0x02: world.signs.emplace_back(t.text, pos); break;
                case 0x03:
                    if (is_tile_lock(t.fg)) { ::area_lock lock{}; lock.pos = pos; lock.owner = owner; lock.ignore_air = (t.a & 0x20); world.area_locks.push_back(lock); }
                    else world_lock = true;
                    break;
                case 0x04: world.trees.emplace_back(now - std::min(t.a, now), static_cast<u_char>(t.b), pos); break;
                case 0x08: world.random_blocks.emplace_back(static_cast<u_char>(t.a), pos); break;
                case 0x09: world.providers.push_back({ pos, now - std::min(t.a, now) }); break;
                case 0x0e: { ::mannequin m{}; m.pos = pos; m.label = t.text; for (int s = 0; s < 9; ++s) m.clothes[s] = static_cast<short>(t.list[s]); world.mannequins.push_back(m); break; }
                case 0x10: { ::game_tile g{}; g.pos = pos; g.team = static_cast<u_char>(t.a); world.game_tiles.push_back(g); break; }
                case 0x17: world.displays.emplace_back(t.a, pos); break;
                case 0x18: { ::vending v{}; v.pos = pos; v.id = static_cast<short>(t.a); v.price = static_cast<int>(t.b); world.vendings.push_back(v); break; } // @note empty: stock isn't in the file
                case 0x28: { ::weather_setting w{}; w.pos = pos; w.item = t.a; world.weather_settings.push_back(w); break; }
                case 0x31: { ::weather_setting w{}; w.pos = pos; w.item = t.a; w.gravity = static_cast<int>(t.b); world.weather_settings.push_back(w); break; }
                case 0x2b: { ::shelf s{}; s.pos = pos; for (int k = 0; k < 4; ++k) s.items[k] = t.list[k]; world.shelves.push_back(s); break; }
                case 0x2f: { ::fish_mount f{}; f.pos = pos; f.label = t.text; f.fish = static_cast<short>(t.a); f.lbs = static_cast<int>(t.b); world.fish_mounts.push_back(f); break; }
                case 0x50: { ::kranken k{}; k.pos = pos; k.color = t.a; world.krankens.push_back(k); break; }
                default: break;
            }
            if (item.type == type::MAIN_DOOR && std::ranges::find(world.doors, pos, &::door::pos) == world.doors.end())
                world.doors.emplace_back("EXIT", "", "", pos);
        }
        world.owner = world_lock ? owner : 0;
        world.access.fill(0);
        world.is_public = false;
        world.lock_state = 0x00;
        for (::area_lock &lock : world.area_locks) world.apply_area_lock(lock, static_cast<u_short>(world.blocks[cord(lock.pos.x_int(), lock.pos.y_int())].fg));
    }

    std::vector<std::string> world_files()
    {
        std::vector<std::string> names{};
        std::error_code ec{};
        if (!fs::is_directory(FOLDER, ec)) return names;
        for (const fs::directory_entry &e : fs::directory_iterator(FOLDER, ec))
            if (e.is_regular_file() && upper(e.path().extension().string()) == ".DAT") names.push_back(e.path().stem().string());
        std::ranges::sort(names);
        return names;
    }

    bool valid_world_name(const std::string &name)
    {
        return !name.empty() && name.length() <= 24 && std::ranges::all_of(name, [](char c) { return std::isalnum(static_cast<u_char>(c)); });
    }
}

void export_command(ENetEvent &event, const std::string_view text)
{
    const std::string name = upper(std::string(command_arg(text)));
    if (!valid_world_name(name)) return say(event, "Usage: /export <`$world``> - saves the world to worlds/<world>.dat");

    auto it = std::ranges::find(worlds, name, &::world::name);
    if (it == worlds.end())
    {
        if (!::world::exists(name)) return say(event, std::format("`4There is no world called {}.``", name));
        it = worlds.emplace(worlds.end(), name);
    }
    std::error_code ec{};
    fs::create_directories(FOLDER, ec);
    const ::blob blob = it->serialize();
    std::ofstream file(FOLDER / (name + ".dat"), std::ios::binary);
    file.write(reinterpret_cast<const char*>(blob.data().data()), static_cast<std::streamsize>(blob.size()));
    if (!file) return say(event, "`4Couldn't write the file.``");
    say(event, std::format("`2Exported {} to worlds/{}.dat ({} bytes).``", name, name, blob.size()));
}

void import_command(ENetEvent &event, const std::string_view text)
{
    const ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const std::string args{ command_arg(text) };
    const std::size_t space = args.find(' ');
    const std::string file = (space == std::string::npos) ? args : args.substr(0, space);
    std::string target = (space == std::string::npos) ? "" : upper(args.substr(space + 1));
    while (!target.empty() && target.back() == ' ') target.pop_back();

    if (file.empty())
    {
        const std::vector<std::string> names = world_files();
        if (names.empty()) return say(event, "No .dat files in the worlds folder next to the server. Put some there, then `$/import <file> <new world>``.");
        std::string list{};
        for (const std::string &n : names) list += (list.empty() ? "" : ", ") + n;
        say(event, std::format("Worlds you can import ({}): `w{}``", names.size(), list));
        return say(event, "Usage: `$/import <file> <new world>`` - builds a new world from worlds/<file>.dat");
    }
    if (target.empty()) return say(event, "Usage: `$/import <file> <new world>``");
    if (!valid_world_name(target)) return say(event, "`4The world name can only have letters and numbers, up to 24.``");
    if (file.find_first_of("/\\.") != std::string::npos) return say(event, "`4Give just the file's name, without a folder or .dat.``");
    if (std::ranges::find(worlds, target, &::world::name) != worlds.end() || ::world::exists(target))
        return say(event, std::format("`4{} already exists. Pick a new name.``", target));

    std::ifstream in(FOLDER / (file + ".dat"), std::ios::binary);
    if (!in) return say(event, std::format("`4There's no worlds/{}.dat.`` Type /import to see the files.", file));
    const std::vector<u_char> data{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };

    world_in w{};
    try { w = read_world(data); }
    catch (const std::exception &e) { return say(event, std::format("`4Couldn't read {}.dat: {}.``", file, e.what())); }
    if (is_carnival_world(w.name) || is_carnival_world(target)) return say(event, "`4CARNIVAL worlds are built by the server and can't be imported.``");
    if (w.width != 100 || w.height != 60) return say(event, std::format("`4{} is {}x{}. Only 100x60 worlds can be imported.``", file, w.width, w.height));

    ::world &world = *worlds.emplace(worlds.end(), target);
    apply(world, w, pPeer->user_id);
    world.save();
    say(event, std::format("`2Imported {}.dat (world {}, version {}) as {}.`` Type `$/warp {}`` to go there.", file, w.name, w.version, target, target));
}
