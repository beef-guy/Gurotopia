#include "pch.hpp"

#include "stb_image.h"
#include "stb_image_write.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "render.hpp"

std::string render::texture_folder{};

namespace
{
    constexpr int CELL = 32;

    struct image
    {
        int w{}, h{};
        std::vector<u_char> rgba{}; // @note top row first

        const u_char *at(int x, int y) const { return &rgba[(static_cast<std::size_t>(y) * w + x) * 4]; }
    };

    std::filesystem::path game_folder()
    {
        if (!render::texture_folder.empty()) return render::texture_folder;
#ifdef _WIN32
        if (const char *local = std::getenv("LOCALAPPDATA"); local != nullptr)
            return std::filesystem::path(local) / "Growtopia" / "game";
#endif
        return "game";
    }

    template<typename T>
    T read_le(const u_char *p) { T v{}; std::memcpy(&v, p, sizeof(T)); return v; }

    /*
    * @brief .rttex: an optional RTPACK (zlib) around an RTTXTR header and raw pixels, bottom row first
    * @note layout: "RTTXTR", version, reserved, height, width, format, original height, original width, uses alpha, compressed, reserved, mipmaps, 64 reserved = 124 bytes
    */
    std::shared_ptr<image> load_rttex(const std::filesystem::path &path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) return nullptr;
        std::vector<u_char> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        if (data.size() >= 32 && std::memcmp(data.data(), "RTPACK", 6) == 0)
        {
            const u_int unpacked = read_le<u_int>(&data[12]);
            if (data[16] != 1/*zlib*/ || unpacked == 0 || unpacked > (64u << 20)) return nullptr;
            int length = 0;
            char *raw = stbi_zlib_decode_malloc_guesssize(reinterpret_cast<const char*>(&data[32]), static_cast<int>(data.size() - 32), static_cast<int>(unpacked), &length);
            if (raw == nullptr) return nullptr;
            data.assign(reinterpret_cast<u_char*>(raw), reinterpret_cast<u_char*>(raw) + length);
            std::free(raw); // @note stb uses malloc
        }
        if (data.size() < 124 || std::memcmp(data.data(), "RTTXTR", 6) != 0) return nullptr;

        const int height = read_le<int>(&data[8]);
        const int width  = read_le<int>(&data[12]);
        const int format = read_le<int>(&data[16]);
        const bool alpha = data[28] != 0;
        const bool compressed = data[29] != 0;
        if (format != 0x1401/*GL_UNSIGNED_BYTE*/ || compressed || width <= 0 || height <= 0 || width > 8192 || height > 8192) return nullptr;

        const int channels = alpha ? 4 : 3;
        if (data.size() < 124 + static_cast<std::size_t>(width) * height * channels) return nullptr;

        auto img = std::make_shared<image>();
        img->w = width, img->h = height;
        img->rgba.resize(static_cast<std::size_t>(width) * height * 4);
        for (int y = 0; y < height; ++y)
        {
            const u_char *src = &data[124 + static_cast<std::size_t>(height - 1 - y) * width * channels]; // @note flip: the file starts at the bottom
            u_char *dst = &img->rgba[static_cast<std::size_t>(y) * width * 4];
            for (int x = 0; x < width; ++x, src += channels, dst += 4)
            {
                dst[0] = src[0], dst[1] = src[1], dst[2] = src[2];
                dst[3] = alpha ? src[3] : 255;
            }
        }
        return img;
    }

    std::mutex cache_mutex;
    std::unordered_map<std::string, std::shared_ptr<image>> cache; // @note nullptr = missing or unreadable

    std::shared_ptr<image> texture(const std::string &name)
    {
        if (name.empty() || name.find("..") != std::string::npos) return nullptr;
        std::lock_guard lock(cache_mutex);
        if (auto it = cache.find(name); it != cache.end()) return it->second;
        return cache[name] = load_rttex(game_folder() / name);
    }

    struct canvas
    {
        int w{}, h{};
        std::vector<u_char> rgb{};

        canvas(int _w, int _h) : w(_w), h(_h), rgb(static_cast<std::size_t>(_w) * _h * 3, 0) {}

        void blend(int x, int y, const u_char *src, int alpha_scale = 255, const u_char *tint = nullptr)
        {
            if (x < 0 || y < 0 || x >= w || y >= h) return;
            const int a = src[3] * alpha_scale / 255;
            if (a == 0) return;
            u_char *dst = &rgb[(static_cast<std::size_t>(y) * w + x) * 3];
            for (int c = 0; c < 3; ++c)
            {
                const int s = tint ? src[c] * tint[c] / 255 : src[c];
                dst[c] = static_cast<u_char>((s * a + dst[c] * (255 - a)) / 255);
            }
        }
        void fill(int x, int y, u_char r, u_char g, u_char b)
        {
            const u_char px[4]{r, g, b, 255};
            blend(x, y, px);
        }

        /* @brief copy a sw x sh piece of img at (sx, sy) to (dx, dy), scaled to dw x dh */
        void draw(const image &img, int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh,
                  bool flip = false, int alpha_scale = 255, const u_char *tint = nullptr)
        {
            for (int y = 0; y < dh; ++y)
            {
                const int py = sy + y * sh / dh;
                if (py < 0 || py >= img.h) continue;
                for (int x = 0; x < dw; ++x)
                {
                    const int px = sx + (flip ? (dw - 1 - x) : x) * sw / dw;
                    if (px < 0 || px >= img.w) continue;
                    blend(dx + x, dy + y, img.at(px, py), alpha_scale, tint);
                }
            }
        }
    };

    /* @note missing neighbours, as bits */
    enum : u_char { N = 1, S = 2, W = 4, E = 8 };

    /* @brief frame for blocks that join on all 8 sides (Dirt, Cave Background, water). 47 frames, 8 per row */
    int frame_eight(u_char miss, bool nw, bool ne, bool sw, bool se) // @note nw..se: that corner neighbour is missing
    {
        switch (miss)
        {
            case 0:
            {
                static constexpr int corners[16]{0, 13, 14, 17, 15, 19, 22, 26, 16, 21, 20, 25, 18, 24, 23, 27}; // @note index: nw | ne<<1 | sw<<2 | se<<3
                return corners[nw | (ne << 1) | (sw << 2) | (se << 3)];
            }
            case N:         return (sw && se) ? 39 : sw ? 37 : se ? 38 : 1;
            case S:         return (nw && ne) ? 42 : nw ? 40 : ne ? 41 : 2;
            case W:         return (ne && se) ? 33 : ne ? 31 : se ? 32 : 3;
            case E:         return (nw && sw) ? 36 : nw ? 34 : sw ? 35 : 4;
            case N|W:       return se ? 45 : 5;
            case N|E:       return sw ? 46 : 6;
            case W|S:       return ne ? 43 : 7;
            case E|S:       return nw ? 44 : 8;
            case W|E:       return 9;
            case N|W|E:     return 10;
            case W|E|S:     return 11;
            case N|S|W|E:   return 12;
            case N|S:       return 28;
            case N|W|S:     return 29;
            case N|E|S:     return 30;
        }
        return 0;
    }

    /* @brief frame for blocks that join on 4 sides (Lava). 16 frames, 8 per row */
    int frame_four(u_char miss)
    {
        static constexpr int frames[16]{
            0,      /* -    */ 1,  /* N    */ 2,  /* S    */ 13, /* NS   */
            3,      /* W    */ 5,  /* NW   */ 7,  /* SW   */ 14, /* NSW  */
            4,      /* E    */ 6,  /* NE   */ 8,  /* SE   */ 15, /* NSE  */
            9,      /* WE   */ 10, /* NWE  */ 11, /* SWE  */ 12  /* NSWE */
        };
        return frames[miss & 15];
    }

    void tint_of(u_char paint, u_char out[3])
    {
        out[0] = out[1] = out[2] = 255;
        switch (paint & 0xe0)
        {
            case S_RED:      out[1] = 110, out[2] = 110; break;
            case S_GREEN:    out[0] = 110, out[2] = 110; break;
            case S_YELLOW:   out[2] = 90; break;
            case S_BLUE:     out[0] = 110, out[1] = 130; break;
            case S_AQUA:     out[0] = 90; break;
            case S_PURPLE:   out[1] = 90; break;
            case S_CHARCOAL: out[0] = out[1] = out[2] = 70; break;
        }
    }

    void colour_of(u_int c, u_char out[3]) // @note items.dat colours are 0xBBGGRRAA
    {
        out[0] = (c >> 8) & 0xff, out[1] = (c >> 16) & 0xff, out[2] = (c >> 24) & 0xff;
    }

    class painter
    {
    public:
        painter(const render::snapshot &_shot) : shot(_shot), out(_shot.width * CELL, _shot.height * CELL) {}

        const render::snapshot &shot;
        canvas out;

        short id_at(int x, int y, bool fg) const
        {
            if (x < 0 || y < 0 || x >= shot.width || y >= shot.height) return -1; // @note outside the world
            const render::tile &t = shot.tiles[y * shot.width + x];
            return fg ? t.fg : t.bg;
        }
        bool water_at(int x, int y) const
        {
            if (x < 0 || y < 0 || x >= shot.width || y >= shot.height) return true;
            return shot.tiles[y * shot.width + x].state3 & S_WATER;
        }
        bool solid_at(int x, int y) const
        {
            const short id = id_at(x, y, true);
            return id > 0 && id_to_item(id).collision == collision::FULL;
        }

        /* @return which frame of an item's sheet this tile uses, as a cell offset from tex_x, tex_y */
        std::pair<int, int> frame(const ::item &item, int x, int y, bool fg) const
        {
            const auto same = [&](int dx, int dy) {
                const short id = id_at(x + dx, y + dy, fg);
                return id == -1 || id == static_cast<short>(item.id); // @note the world's edge counts as joined
            };
            const auto row8 = [](int i) { return std::pair{i % 8, i / 8}; };
            switch (item.spread)
            {
                case 2: // @note eight ways
                {
                    u_char miss = (same(0, -1) ? 0 : N) | (same(0, 1) ? 0 : S) | (same(-1, 0) ? 0 : W) | (same(1, 0) ? 0 : E);
                    return row8(frame_eight(miss, !same(-1, -1), !same(1, -1), !same(-1, 1), !same(1, 1)));
                }
                case 5: // @note four ways
                {
                    u_char miss = (same(0, -1) ? 0 : N) | (same(0, 1) ? 0 : S) | (same(-1, 0) ? 0 : W) | (same(1, 0) ? 0 : E);
                    return row8(frame_four(miss));
                }
                case 3: // @note sideways: left end, middle, right end, alone
                {
                    const bool l = same(-1, 0), r = same(1, 0);
                    return {(l && r) ? 1 : r ? 0 : l ? 2 : 3, 0};
                }
                case 7: // @note up and down: bottom end, middle, top end, alone
                {
                    const bool u = same(0, -1), d = same(0, 1);
                    return {(u && d) ? 1 : u ? 0 : d ? 2 : 3, 0};
                }
                case 4: // @note sticks to the block below, left, right or above
                {
                    if (solid_at(x, y + 1)) return {3, 0};
                    if (solid_at(x - 1, y)) return {0, 0};
                    if (solid_at(x + 1, y)) return {2, 0};
                    if (solid_at(x, y - 1)) return {1, 0};
                    return {4, 0};
                }
                case 6: // @note random look, fixed per tile
                    return {static_cast<int>((static_cast<u_int>(x) * 73856093u ^ static_cast<u_int>(y) * 19349663u) % 4u), 0};
            }
            return {0, 0};
        }

        /* @return the frame shown when the item is on its own (drops) */
        static std::pair<int, int> lone_frame(const ::item &item)
        {
            switch (item.spread)
            {
                case 2: case 5: return {4, 1}; // @note frame 12
                case 3: case 7: return {3, 0};
                case 4: return {4, 0};
            }
            return {0, 0};
        }

        void seed_icon(const ::item &item, int dx, int dy, int size)
        {
            auto seeds = texture("seed.rttex");
            if (!seeds) return;
            u_char base[3], over[3];
            colour_of(item.seed_color, base);
            colour_of(item.seed_overlay_color, over);
            out.draw(*seeds, item.seed_base * 16, 0, 16, 16, dx, dy, size, size, false, 255, base);
            out.draw(*seeds, item.seed_overlay * 16, 16, 16, 16, dx, dy, size, size, false, 255, over);
        }

        /* @brief a planted seed: a trunk and a round top in the seed's colours */
        void tree(const ::item &item, int px, int py)
        {
            u_char leaf[3], spot[3];
            colour_of(item.seed_color, leaf);
            colour_of(item.seed_overlay_color, spot);
            for (int y = 14; y < 32; ++y)
                for (int x = 13; x < 19; ++x)
                    (x == 13 || x == 18) ? out.fill(px + x, py + y, 70, 44, 22) : out.fill(px + x, py + y, 120, 78, 40);
            for (int y = 0; y < 26; ++y)
                for (int x = 2; x < 30; ++x)
                {
                    const float d = std::hypot(x - 15.5f, (y - 12.0f) * 1.15f);
                    if (d > 13.5f) continue;
                    if (d > 12.5f) out.fill(px + x, py + y, leaf[0] / 2, leaf[1] / 2, leaf[2] / 2);
                    else if (((x * 7 + y * 13) % 23) == 0) out.fill(px + x, py + y, spot[0], spot[1], spot[2]);
                    else
                    {
                        const int shade = (x + y < 20) ? 40 : 0; // @note a lighter top left
                        out.fill(px + x, py + y, static_cast<u_char>(std::min(255, leaf[0] + shade)), static_cast<u_char>(std::min(255, leaf[1] + shade)), static_cast<u_char>(std::min(255, leaf[2] + shade)));
                    }
                }
        }

        void sky()
        {
            /* @note the sunny day sky: deeper blue at the top */
            for (int y = 0; y < out.h; ++y)
            {
                const int t = y * 255 / std::max(1, out.h - 1);
                const u_char r = static_cast<u_char>(60 + (150 - 60) * t / 255), g = static_cast<u_char>(150 + (220 - 150) * t / 255), b = static_cast<u_char>(232 + (255 - 232) * t / 255);
                for (int x = 0; x < out.w; ++x) out.fill(x, y, r, g, b);
            }

            if (auto sun = texture("sun.rttex"); sun) out.draw(*sun, 0, 0, 161, 159, out.w - 360, 80, 161 * 3 / 2, 159 * 3 / 2);

            /* @note the hills sit on the ground line: the first row where most of the world is built */
            int ground = shot.height * 2 / 5;
            for (int y = 0; y < shot.height; ++y)
            {
                int filled = 0;
                for (int x = 0; x < shot.width; ++x)
                    if (shot.tiles[y * shot.width + x].fg != 0 || shot.tiles[y * shot.width + x].bg != 0) ++filled;
                if (filled * 2 > shot.width) { ground = y; break; }
            }
            const int bottom = ground * CELL + CELL;
            const char *layers[3]{"hills3.rttex", "hills2.rttex", "hills1.rttex"};
            for (int i = 0; i < 3; ++i)
            {
                auto hill = texture(layers[i]);
                if (!hill) continue;
                const int w = hill->w * 3 / 2, h = hill->h * 3 / 2;
                for (int x = -(i * 211) % w; x < out.w; x += w)
                    out.draw(*hill, 0, 0, hill->w, hill->h, x, bottom - h + i * 40, w, h);
            }
        }

        void layer(bool fg)
        {
            for (int y = 0; y < shot.height; ++y)
                for (int x = 0; x < shot.width; ++x)
                {
                    const render::tile &t = shot.tiles[y * shot.width + x];
                    const short id = fg ? t.fg : t.bg;
                    if (id <= 0) continue;
                    const ::item &item = id_to_item(id);
                    if (item.id != static_cast<u_short>(id)) continue;
                    if (fg && item.type == type::SEED) { tree(item, x * CELL, y * CELL); continue; }

                    auto tex = texture(item.texture);
                    if (!tex) continue;
                    const auto [fx, fy] = frame(item, x, y, fg);
                    u_char tint[3];
                    tint_of(t.state3, tint);
                    const bool painted = (t.state3 & 0xe0) != 0;
                    out.draw(*tex, (item.tex_x + fx) * CELL, (item.tex_y + fy) * CELL, CELL, CELL, x * CELL, y * CELL, CELL, CELL,
                             fg && (t.state2 & S_LEFT), 255, painted ? tint : nullptr);
                }
        }

        void water()
        {
            auto tex = texture("water.rttex");
            for (int y = 0; y < shot.height; ++y)
                for (int x = 0; x < shot.width; ++x)
                {
                    if (!(shot.tiles[y * shot.width + x].state3 & S_WATER)) continue;
                    if (!tex) { for (int py = 0; py < CELL; ++py) for (int px = 0; px < CELL; ++px) { const u_char c[4]{40, 140, 230, 140}; out.blend(x * CELL + px, y * CELL + py, c); } continue; }
                    u_char miss = (water_at(x, y - 1) ? 0 : N) | (water_at(x, y + 1) ? 0 : S) | (water_at(x - 1, y) ? 0 : W) | (water_at(x + 1, y) ? 0 : E);
                    const int f = frame_eight(miss, !water_at(x - 1, y - 1), !water_at(x + 1, y - 1), !water_at(x - 1, y + 1), !water_at(x + 1, y + 1));
                    out.draw(*tex, (f % 8) * CELL, (f / 8) * CELL, CELL, CELL, x * CELL, y * CELL, CELL, CELL, false, 170);
                }
        }

        void drops()
        {
            for (const render::drop &d : shot.drops)
            {
                const ::item &item = id_to_item(d.id);
                if (item.id != d.id) continue;
                const int x = static_cast<int>(d.x), y = static_cast<int>(d.y);
                if (item.type == type::SEED) { seed_icon(item, x, y, 16); continue; }
                auto tex = texture(item.texture);
                if (!tex) continue;
                const auto [fx, fy] = lone_frame(item);
                out.draw(*tex, (item.tex_x + fx) * CELL, (item.tex_y + fy) * CELL, CELL, CELL, x, y, 16, 16);
            }
        }
    };

    void png_write(void *context, void *data, int size)
    {
        auto *bytes = static_cast<std::vector<u_char>*>(context);
        bytes->insert(bytes->end(), static_cast<u_char*>(data), static_cast<u_char*>(data) + size);
    }
}

render::snapshot render::take(const ::world &world)
{
    snapshot shot{.name = world.name, .width = 100, .height = static_cast<int>(world.blocks.size() / 100)};
    shot.tiles.reserve(world.blocks.size());
    for (const ::block &b : world.blocks)
        shot.tiles.push_back({b.fg, b.bg, b.state[2], b.state[3]});
    for (const ::object &o : world.objects)
        shot.drops.push_back({o.id, o.pos.x, o.pos.y});
    return shot;
}

std::vector<u_char> render::draw(const snapshot &shot, std::string &error)
{
    if (shot.height <= 0 || shot.tiles.size() < static_cast<std::size_t>(shot.width * shot.height))
    {
        error = "this world has no tiles";
        return {};
    }
    if (!texture("tiles_page1.rttex"))
    {
        error = std::format("the game textures aren't in {}. start the server with --textures <your Growtopia\\game folder>", game_folder().string());
        return {};
    }

    painter p(shot);
    p.sky();
    p.layer(false);
    p.layer(true);
    p.water();
    p.drops();

    std::vector<u_char> png{};
    stbi_write_png_to_func(png_write, &png, p.out.w, p.out.h, 3, p.out.rgb.data(), p.out.w * 3);

    /* @note Discord takes files up to 10 MB: halve the size if it's too big */
    if (png.size() > 9u * 1024u * 1024u)
    {
        const int w = p.out.w / 2, h = p.out.h / 2;
        std::vector<u_char> half(static_cast<std::size_t>(w) * h * 3);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                for (int c = 0; c < 3; ++c)
                {
                    const auto px = [&](int sx, int sy) { return p.out.rgb[(static_cast<std::size_t>(sy) * p.out.w + sx) * 3 + c]; };
                    half[(static_cast<std::size_t>(y) * w + x) * 3 + c] = static_cast<u_char>((px(x * 2, y * 2) + px(x * 2 + 1, y * 2) + px(x * 2, y * 2 + 1) + px(x * 2 + 1, y * 2 + 1)) / 4);
                }
        png.clear();
        stbi_write_png_to_func(png_write, &png, w, h, 3, half.data(), w * 3);
    }
    if (png.empty()) error = "the PNG could not be made";
    return png;
}
