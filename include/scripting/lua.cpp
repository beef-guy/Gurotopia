#include "pch.hpp"

#include "lua/lua.h"
#include "lua/lauxlib.h"
#include "lua/lualib.h"

#include "onVariant/ConsoleMessage.hpp"
#include "onVariant/SetBux.hpp"
#include "action/join_request.hpp"
#include "action/quit_to_exit.hpp"
#include "commands/__command.hpp"
#include "commands/moderation.hpp"
#include "database/roles.hpp"
#include "database/platform.hpp"
#include "tools/sound.hpp"
#include "tools/terminal.hpp"

#include "lua.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstring>
#include <ctime>
#include <functional>
#include <map>
#include <unordered_map>
#include <unordered_set>

std::size_t lua::memory_limit_mb = 64;

namespace
{
    using clock = std::chrono::steady_clock;

    /* @note how long one event, timer or command may run. the game waits for it, so keep it short */
    constexpr auto EVENT_BUDGET = std::chrono::milliseconds(100);
    /* @note how long a script may take to load (the code outside functions) */
    constexpr auto LOAD_BUDGET = std::chrono::milliseconds(1000);
    constexpr int HOOK_EVERY = 1000; // @note instructions between clock checks

    constexpr std::size_t MAX_SOURCE = 256 * 1024;
    constexpr std::size_t MAX_TIMERS = 200;
    constexpr std::size_t MAX_COMMANDS = 200;
    constexpr std::size_t MAX_KEY = 64;
    constexpr std::size_t MAX_VALUE = 4000;
    constexpr std::size_t MAX_KEYS = 500;       // @note per player, and for the server
    constexpr std::size_t MAX_TEXT = 2000;      // @note one message, bubble...
    constexpr std::size_t MAX_DIALOG = 16 * 1024;
    constexpr int LOGS_PER_MINUTE = 120;
    constexpr int LOG_KEEP = 1000;              // @note rows kept in lua_log

    enum event_id : int { EV_START, EV_JOIN, EV_LEAVE, EV_ENTER, EV_EXIT, EV_CHAT, EV_PUNCH, EV_BREAK, EV_PLACE, EV_WRENCH, EV_DIALOG, EV_COUNT };
    constexpr const char *EVENT_NAMES[EV_COUNT] = { "start", "join", "leave", "enterWorld", "leaveWorld", "chat", "punch", "break", "place", "wrench", "dialog" };

    struct handler { int ref; std::string script; };
    struct command_t { int ref; std::string script, description; u_char role; };
    struct script_timer { int id; int ref; std::string script; long long next; long long every; /* ms; 0 = once */ int fails{}; };
    struct log_row { std::string script, level, message; };

    /* @note persistent values: "" is the server's own, a user id is that player's */
    struct store_t
    {
        std::unordered_map<std::string, std::string> values{};
        std::unordered_set<std::string> dirty{};
        std::unordered_set<std::string> removed{};
    };

    struct engine_t
    {
        lua_State *L{};
        std::size_t used{}, limit{};
        clock::time_point deadline{};
        std::chrono::milliseconds budget{EVENT_BUDGET};
        bool killed{};  // @note out of time: the error can't be caught by pcall any more
        int depth{};
        std::string script{}; // @note the script running now, for print() and errors
        std::array<std::vector<handler>, EV_COUNT> handlers{};
        std::map<std::string, command_t> commands{};
        std::vector<script_timer> timers{};
        int next_timer{1};
        std::vector<std::string> loaded{};
    } E;

    std::vector<std::function<void()>> later{}; // @note warps and kicks, run once the packet that asked for them is handled
    std::vector<log_row> pending_log{};
    int logs_this_minute{}, logs_skipped{};
    clock::time_point log_minute{}, last_flush{}, last_trim{};
    std::unordered_map<std::string, store_t> stores{};
    bool tables_ready{};

    std::string lower(std::string text)
    {
        for (char &c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return text;
    }

    long long now_ms()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch()).count();
    }

    std::string escape(const std::string &text)
    {
        std::string out(text.size() * 2 + 1, '\0');
        out.resize(mysql_real_escape_string(db.mysql, out.data(), text.c_str(), text.size()));
        return out;
    }

    void script_log(const std::string &script, const std::string &level, std::string message)
    {
        if (message.size() > 990) message = message.substr(0, 990) + "...";
        print(std::format("[lua] {} {}: {}", script, level, message));
        if (clock::now() - log_minute >= std::chrono::minutes(1)) { log_minute = clock::now(); logs_this_minute = 0; }
        if (++logs_this_minute > LOGS_PER_MINUTE) { ++logs_skipped; return; }
        pending_log.push_back({script, level, std::move(message)});
    }

    void flush_log()
    {
        if (!tables_ready) return;
        if (logs_skipped > 0)
        {
            pending_log.push_back({"(server)", "warn", std::format("{} more messages were skipped. A script is printing too much.", logs_skipped)});
            logs_skipped = 0;
        }
        if (pending_log.empty()) return;
        std::string values{};
        for (const log_row &row : pending_log)
        {
            if (!values.empty()) values += ',';
            values += std::format("('{}','{}','{}')", escape(row.script.substr(0, 48)), escape(row.level), escape(row.message));
        }
        pending_log.clear();
        mysql_query(db.mysql, ("INSERT INTO lua_log (script, level, message) VALUES " + values).c_str());
    }

    void trim_log()
    {
        if (!tables_ready || mysql_query(db.mysql, "SELECT MAX(id) FROM lua_log")) return;
        MYSQL_RES *res = mysql_store_result(db.mysql);
        if (res == nullptr) return;
        MYSQL_ROW row = mysql_fetch_row(res);
        const long long top = (row && row[0]) ? std::atoll(row[0]) : 0;
        mysql_free_result(res);
        if (top > LOG_KEEP) mysql_query(db.mysql, std::format("DELETE FROM lua_log WHERE id <= {}", top - LOG_KEEP).c_str());
    }

    /* ---------- the player's and the server's saved values ---------- */

    store_t &store(const std::string &scope)
    {
        if (auto it = stores.find(scope); it != stores.end()) return it->second;
        store_t &fresh = stores[scope];
        if (tables_ready && !mysql_query(db.mysql, std::format("SELECT k, v FROM lua_data WHERE scope = '{}' LIMIT {}", escape(scope), MAX_KEYS).c_str()))
        {
            if (MYSQL_RES *res = mysql_store_result(db.mysql); res != nullptr)
            {
                while (MYSQL_ROW row = mysql_fetch_row(res))
                    if (row[0] && row[1]) fresh.values[row[0]] = row[1];
                mysql_free_result(res);
            }
        }
        return fresh;
    }

    void flush_store(const std::string &scope, store_t &s)
    {
        if (!tables_ready) return;
        if (!s.dirty.empty())
        {
            std::string values{};
            for (const std::string &k : s.dirty)
            {
                auto it = s.values.find(k);
                if (it == s.values.end()) continue;
                if (!values.empty()) values += ',';
                values += std::format("('{}','{}','{}')", escape(scope), escape(k), escape(it->second));
            }
            if (!values.empty()) mysql_query(db.mysql, ("REPLACE INTO lua_data (scope, k, v) VALUES " + values).c_str());
            s.dirty.clear();
        }
        for (const std::string &k : s.removed)
            mysql_query(db.mysql, std::format("DELETE FROM lua_data WHERE scope = '{}' AND k = '{}'", escape(scope), escape(k)).c_str());
        s.removed.clear();
    }

    void flush_stores()
    {
        for (auto &[scope, s] : stores) flush_store(scope, s);
    }

    /* ---------- finding players and worlds again ---------- */

    struct ud_player { int uid; char growid[24]; };
    struct ud_world { char name[32]; };

    ENetPeer *leaving = nullptr; // @note the player logging out during the leave event (ENet no longer lists them as connected)

    ENetPeer *peer_by_uid(int uid)
    {
        if (leaving != nullptr && leaving->data != nullptr && static_cast<::peer*>(leaving->data)->user_id == uid) return leaving;
        ENetPeer *found = nullptr;
        peers("", PeerSearch::EVERYONE, [&](ENetPeer *p)
        {
            const ::peer *pp = static_cast<::peer*>(p->data);
            if (found == nullptr && pp != nullptr && pp->authenticated && pp->user_id == uid) found = p;
        });
        return found;
    }

    ::world *world_by_name(const std::string &name)
    {
        auto it = std::ranges::find(worlds, name, &::world::name);
        return it == worlds.end() ? nullptr : &*it;
    }

    std::string current_world(const ::peer &p)
    {
        return p.netid == 0 ? std::string{} : p.recent_worlds.back();
    }

    void push_player(lua_State *L, ENetPeer *peer)
    {
        const ::peer *pp = static_cast<::peer*>(peer->data);
        auto *ud = static_cast<ud_player*>(lua_newuserdatauv(L, sizeof(ud_player), 0));
        ud->uid = pp->user_id;
        std::snprintf(ud->growid, sizeof(ud->growid), "%s", pp->growid.c_str());
        luaL_setmetatable(L, "psnode.player");
    }

    void push_world(lua_State *L, const std::string &name)
    {
        auto *ud = static_cast<ud_world*>(lua_newuserdatauv(L, sizeof(ud_world), 0));
        std::snprintf(ud->name, sizeof(ud->name), "%s", name.c_str());
        luaL_setmetatable(L, "psnode.world");
    }

    /* @return the player, or nullptrs once they logged off (then the method quietly does nothing, so a timer
     * that runs after someone left doesn't fill the log with errors) */
    std::pair<ENetPeer*, ::peer*> check_player(lua_State *L, int index = 1)
    {
        auto *ud = static_cast<ud_player*>(luaL_checkudata(L, index, "psnode.player"));
        ENetPeer *peer = peer_by_uid(ud->uid);
        if (peer == nullptr) return { nullptr, nullptr };
        return { peer, static_cast<::peer*>(peer->data) };
    }

    ::world &check_world(lua_State *L, int index = 1)
    {
        auto *ud = static_cast<ud_world*>(luaL_checkudata(L, index, "psnode.world"));
        ::world *w = world_by_name(ud->name);
        if (w == nullptr) luaL_error(L, "the world %s is not open (nobody is in it)", ud->name);
        return *w;
    }

    std::string check_text(lua_State *L, int index, std::size_t max = MAX_TEXT)
    {
        std::size_t len = 0;
        const char *s = luaL_tolstring(L, index, &len);
        std::string text(s, std::min(len, max));
        lua_pop(L, 1);
        return text;
    }

    short check_item(lua_State *L, int index)
    {
        const lua_Integer id = luaL_checkinteger(L, index);
        if (id <= 0 || id >= static_cast<lua_Integer>(items.size()) || id > 32767) luaL_error(L, "there is no item %d", static_cast<int>(id));
        return static_cast<short>(id);
    }

    int check_count(lua_State *L, int index, int max = 200)
    {
        const lua_Integer n = luaL_optinteger(L, index, 1);
        if (n < 1 || n > max) luaL_error(L, "the amount must be 1 to %d", max);
        return static_cast<int>(n);
    }

    /* @note a C++ exception becomes a normal script error (Lua's own errors pass straight through) */
    template <int (*F)(lua_State*)>
    int guard(lua_State *L)
    {
        std::string why{};
        try { return F(L); }
        catch (const std::exception &exc) { why = exc.what(); }
        return luaL_error(L, "%s", why.c_str());
    }

    /* ---------- saved values ---------- */

    void push_value(lua_State *L, const std::string &v)
    {
        if (v.empty()) { lua_pushnil(L); return; }
        switch (v[0])
        {
            case 'i': lua_pushinteger(L, std::strtoll(v.c_str() + 1, nullptr, 10)); break;
            case 'n': lua_pushnumber(L, std::strtod(v.c_str() + 1, nullptr)); break;
            case 'b': lua_pushboolean(L, v.size() > 1 && v[1] == '1'); break;
            case 's': lua_pushlstring(L, v.data() + 1, v.size() - 1); break;
            default: lua_pushnil(L);
        }
    }

    int store_get(lua_State *L, const std::string &scope, int key_index)
    {
        const std::string key = luaL_checkstring(L, key_index);
        store_t &s = store(scope);
        auto it = s.values.find(key);
        if (it == s.values.end()) lua_pushvalue(L, key_index + 1); // @note the default, or nil
        else push_value(L, it->second);
        return 1;
    }

    int store_set(lua_State *L, const std::string &scope, int key_index)
    {
        std::size_t klen = 0;
        const char *k = luaL_checklstring(L, key_index, &klen);
        if (klen == 0 || klen > MAX_KEY) return luaL_error(L, "a saved value's name must be 1 to %d letters", static_cast<int>(MAX_KEY));
        const std::string key(k, klen);
        std::string value{};
        switch (lua_type(L, key_index + 1))
        {
            case LUA_TNIL: case LUA_TNONE: break;
            case LUA_TBOOLEAN: value = lua_toboolean(L, key_index + 1) ? "b1" : "b0"; break;
            case LUA_TNUMBER:
                value = lua_isinteger(L, key_index + 1) ? std::format("i{}", lua_tointeger(L, key_index + 1)) : std::format("n{:.17g}", lua_tonumber(L, key_index + 1));
                break;
            case LUA_TSTRING:
            {
                std::size_t len = 0;
                const char *s = lua_tolstring(L, key_index + 1, &len);
                if (len > MAX_VALUE) return luaL_error(L, "a saved text can be at most %d letters", static_cast<int>(MAX_VALUE));
                value = "s" + std::string(s, len);
                break;
            }
            default: return luaL_error(L, "only text, numbers and true/false can be saved (not a %s)", luaL_typename(L, key_index + 1));
        }
        store_t &s = store(scope);
        if (value.empty())
        {
            if (s.values.erase(key) > 0) { s.removed.insert(key); s.dirty.erase(key); }
            return 0;
        }
        if (!s.values.contains(key) && s.values.size() >= MAX_KEYS) return luaL_error(L, "too many saved values (at most %d)", static_cast<int>(MAX_KEYS));
        s.values[key] = std::move(value);
        s.dirty.insert(key);
        s.removed.erase(key);
        return 0;
    }

    /* ---------- player ---------- */

    int p_message(lua_State *L)
    {
        auto [peer, pp] = check_player(L);
        if (peer == nullptr) return 0;
        on::ConsoleMessage(peer, check_text(L, 2));
        return 0;
    }

    int p_bubble(lua_State *L)
    {
        auto [peer, pp] = check_player(L);
        if (peer == nullptr) return 0;
        const std::string text = check_text(L, 2);
        if (pp->netid == 0) on::ConsoleMessage(peer, text);
        else send_varlist(peer, { "OnTalkBubble", pp->netid, text, 0u, 1u });
        return 0;
    }

    int p_say(lua_State *L)
    {
        auto [peer, pp] = check_player(L);
        if (peer == nullptr) return 0;
        if (pp->netid == 0) return 0;
        const std::string text = check_text(L, 2, 120);
        const std::string bubble = std::format("CP:0_PL:0_OID:_player_chat={}", text);
        const std::string line = std::format("CP:0_PL:0_OID:_CT:[W]_ `6<{}>`` `$`${}````", pp->display_growid, text);
        const int netid = pp->netid;
        peers(current_world(*pp), PeerSearch::WORLD, [&](ENetPeer *p)
        {
            send_varlist(p, { "OnTalkBubble", netid, bubble, 0u });
            on::ConsoleMessage(p, line, netid);
        });
        return 0;
    }

    int p_overlay(lua_State *L)
    {
        auto [peer, pp] = check_player(L);
        if (peer == nullptr) return 0;
        send_varlist(peer, { "OnTextOverlay", check_text(L, 2) });
        return 0;
    }

    int p_give(lua_State *L)
    {
        auto [peer, pp] = check_player(L);
        if (peer == nullptr) { lua_pushinteger(L, 0); return 1; }
        const short id = check_item(L, 2);
        const int count = check_count(L, 3);
        if (!pp->can_hold(id, count)) { lua_pushinteger(L, 0); return 1; }
        ENetEvent ev{}; ev.peer = peer;
        modify_item_inventory(ev, ::slot(id, static_cast<short>(count)));
        lua_pushinteger(L, count);
        return 1;
    }

    int p_take(lua_State *L)
    {
        auto [peer, pp] = check_player(L);
        if (peer == nullptr) { lua_pushinteger(L, 0); return 1; }
        const short id = check_item(L, 2);
        const int count = check_count(L, 3, 1000000);
        const int taken = std::min<int>(count, pp->have_slot(::slot(id, 0)));
        if (taken > 0)
        {
            ENetEvent ev{}; ev.peer = peer;
            modify_item_inventory(ev, ::slot(id, static_cast<short>(-taken)));
        }
        lua_pushinteger(L, taken);
        return 1;
    }

    int p_count(lua_State *L)
    {
        auto [peer, pp] = check_player(L);
        if (peer == nullptr) { lua_pushinteger(L, 0); return 1; }
        lua_pushinteger(L, pp->have_slot(::slot(check_item(L, 2), 0)));
        return 1;
    }

    int p_has(lua_State *L)
    {
        auto [peer, pp] = check_player(L);
        if (peer == nullptr) { lua_pushboolean(L, 0); return 1; }
        const short id = check_item(L, 2);
        const lua_Integer need = luaL_optinteger(L, 3, 1);
        lua_pushboolean(L, pp->have_slot(::slot(id, 0)) >= need);
        return 1;
    }

    int p_inventory(lua_State *L)
    {
        auto [peer, pp] = check_player(L);
        if (peer == nullptr) { lua_newtable(L); return 1; }
        lua_createtable(L, static_cast<int>(pp->slots.size()), 0);
        int i = 0;
        for (const ::slot &s : pp->slots)
        {
            if (s.id == 0 || s.count <= 0) continue;
            lua_createtable(L, 0, 2);
            lua_pushinteger(L, s.id); lua_setfield(L, -2, "id");
            lua_pushinteger(L, s.count); lua_setfield(L, -2, "count");
            lua_rawseti(L, -2, ++i);
        }
        return 1;
    }

    int p_addgems(lua_State *L)
    {
        auto [peer, pp] = check_player(L);
        if (peer == nullptr) { lua_pushinteger(L, 0); return 1; }
        const lua_Integer n = luaL_checkinteger(L, 2);
        if (n < -1000000000LL || n > 1000000000LL) return luaL_error(L, "at most a billion gems at once");
        pp->gems = std::clamp<long long>(pp->gems + n, 0, 100000000000LL);
        ENetEvent ev{}; ev.peer = peer;
        on::SetBux(ev);
        lua_pushinteger(L, pp->gems);
        return 1;
    }

    int p_setgems(lua_State *L)
    {
        auto [peer, pp] = check_player(L);
        if (peer == nullptr) return 0;
        const lua_Integer n = luaL_checkinteger(L, 2);
        pp->gems = std::clamp<long long>(n, 0, 100000000000LL);
        ENetEvent ev{}; ev.peer = peer;
        on::SetBux(ev);
        return 0;
    }

    int p_addxp(lua_State *L)
    {
        auto [peer, pp] = check_player(L);
        if (peer == nullptr) return 0;
        const lua_Integer n = luaL_checkinteger(L, 2);
        if (n < 1 || n > 10000) return luaL_error(L, "XP must be 1 to 10000");
        ENetEvent ev{}; ev.peer = peer;
        pp->add_xp(ev, static_cast<u_short>(n));
        return 0;
    }

    int p_warp(lua_State *L)
    {
        auto [peer, pp] = check_player(L);
        if (peer == nullptr) return 0;
        std::string name = luaL_checkstring(L, 2);
        if (name.empty() || name.size() > 24 || !isalnum(name)) return luaL_error(L, "a world name is 1 to 24 letters and digits");
        for (char &c : name) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        const int uid = pp->user_id;
        later.push_back([uid, name]()
        {
            ENetPeer *p = peer_by_uid(uid);
            if (p == nullptr) return;
            ENetEvent ev{}; ev.peer = p;
            if (name == "EXIT") action::quit_to_exit(ev, "", false);
            else action::join_request(ev, "", name);
        });
        return 0;
    }

    int p_teleport(lua_State *L)
    {
        auto [peer, pp] = check_player(L);
        if (peer == nullptr) return 0;
        const lua_Integer x = luaL_checkinteger(L, 2), y = luaL_checkinteger(L, 3);
        if (pp->netid == 0) return luaL_error(L, "%s is not in a world", pp->growid.c_str());
        ::world *w = world_by_name(current_world(*pp));
        if (w == nullptr || !valid_tile(*w, ::pos{static_cast<int>(x), static_cast<int>(y)})) return luaL_error(L, "%d, %d is outside the world", static_cast<int>(x), static_cast<int>(y));
        const ::pos to{static_cast<float>(x) * 32.0f, static_cast<float>(y) * 32.0f};
        pp->pos = to;
        const int netid = pp->netid;
        peers(w->name, PeerSearch::WORLD, [&](ENetPeer *p) { send_varlist(p, ::VariantList{ "OnSetPos", CL_Vec2f{to.x, to.y} }, netid); });
        return 0;
    }

    int p_kick(lua_State *L)
    {
        auto [peer, pp] = check_player(L);
        if (peer == nullptr) return 0;
        const std::string reason = lua_isnoneornil(L, 2) ? "" : check_text(L, 2, 100);
        const int uid = pp->user_id;
        later.push_back([uid, reason]()
        {
            ENetPeer *p = peer_by_uid(uid);
            if (p == nullptr) return;
            on::ConsoleMessage(p, std::format("`4You were kicked.``{}", reason.empty() ? "" : " Reason: " + reason));
            send_gameMessage(p, {.message = "action|logon_fail\n"});
            enet_peer_disconnect_later(p, 0);
        });
        return 0;
    }

    int p_sound(lua_State *L)
    {
        auto [peer, pp] = check_player(L);
        if (peer == nullptr) return 0;
        const std::string file = luaL_checkstring(L, 2);
        if (file.empty() || file.size() > 64 || file.find("..") != std::string::npos ||
            file.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_/.-") != std::string::npos)
            return luaL_error(L, "a sound is a file like audio/success.wav");
        play_sfx(peer, file);
        return 0;
    }

    int p_dialog(lua_State *L)
    {
        auto [peer, pp] = check_player(L);
        if (peer == nullptr) return 0;
        std::size_t len = 0;
        const char *text = luaL_checklstring(L, 2, &len);
        if (len > MAX_DIALOG) return luaL_error(L, "the dialog is too big");
        send_varlist(peer, { "OnDialogRequest", std::string(text, len) });
        return 0;
    }

    int p_get(lua_State *L)
    {
        auto [peer, pp] = check_player(L);
        if (peer == nullptr) { lua_pushvalue(L, 3); return 1; }
        return store_get(L, std::to_string(pp->user_id), 2);
    }

    int p_set(lua_State *L)
    {
        auto [peer, pp] = check_player(L);
        if (peer == nullptr) return 0;
        return store_set(L, std::to_string(pp->user_id), 2);
    }

    int p_isstaff(lua_State *L)
    {
        auto [peer, pp] = check_player(L);
        if (peer == nullptr) { lua_pushboolean(L, 0); return 1; }
        lua_pushboolean(L, pp->role >= MODERATOR);
        return 1;
    }

    int p_isdeveloper(lua_State *L)
    {
        auto [peer, pp] = check_player(L);
        if (peer == nullptr) { lua_pushboolean(L, 0); return 1; }
        lua_pushboolean(L, pp->role >= DEVELOPER);
        return 1;
    }

    int p_index(lua_State *L)
    {
        auto *ud = static_cast<ud_player*>(luaL_checkudata(L, 1, "psnode.player"));
        const char *key = luaL_checkstring(L, 2);
        /* @note methods first */
        luaL_getmetatable(L, "psnode.player");
        lua_getfield(L, -1, "methods");
        lua_getfield(L, -1, key);
        if (!lua_isnil(L, -1)) return 1;
        lua_pop(L, 3);

        const std::string_view k = key;
        if (k == "name" || k == "growid") { lua_pushstring(L, ud->growid); return 1; }
        if (k == "id") { lua_pushinteger(L, ud->uid); return 1; }
        ENetPeer *peer = peer_by_uid(ud->uid);
        if (k == "online") { lua_pushboolean(L, peer != nullptr); return 1; }
        if (peer == nullptr) { lua_pushnil(L); return 1; } // @note they logged off
        const ::peer *pp = static_cast<::peer*>(peer->data);
        if (k == "world") { if (pp->netid == 0) lua_pushnil(L); else push_world(L, current_world(*pp)); return 1; }
        if (k == "worldName") { lua_pushstring(L, current_world(*pp).c_str()); return 1; }
        if (k == "x") { lua_pushinteger(L, static_cast<lua_Integer>(std::floor(pp->pos.x / 32.0f))); return 1; }
        if (k == "y") { lua_pushinteger(L, static_cast<lua_Integer>(std::floor(pp->pos.y / 32.0f))); return 1; }
        if (k == "level") { lua_pushinteger(L, pp->level.front()); return 1; }
        if (k == "gems") { lua_pushinteger(L, pp->gems); return 1; }
        if (k == "role") { lua_pushstring(L, roles::get(pp->role_id).name.c_str()); return 1; }
        if (k == "staff") { lua_pushboolean(L, pp->role >= MODERATOR); return 1; }
        if (k == "country") { lua_pushstring(L, pp->country.c_str()); return 1; }
        lua_pushnil(L);
        return 1;
    }

    int p_eq(lua_State *L)
    {
        auto *a = static_cast<ud_player*>(luaL_testudata(L, 1, "psnode.player"));
        auto *b = static_cast<ud_player*>(luaL_testudata(L, 2, "psnode.player"));
        lua_pushboolean(L, a && b && a->uid == b->uid);
        return 1;
    }

    int p_tostring(lua_State *L)
    {
        auto *ud = static_cast<ud_player*>(luaL_checkudata(L, 1, "psnode.player"));
        lua_pushstring(L, ud->growid);
        return 1;
    }

    /* ---------- world ---------- */

    /* @note blocks a script may put down or take away: nothing with settings of its own (locks, doors, machines, trees) */
    bool plain_block(short id)
    {
        if (id == 0) return true;
        if (id == 8/*Bedrock*/) return false;
        switch (id_to_item(id).type)
        {
            case type::FOREGROUND: case type::BACKGROUND: case type::PLATFORM: case type::DEADLY: case type::BOUNCY:
            case type::SLIPPERY: case type::TRAMPOLINE: case type::ANIMATED: case type::STRONG: case type::FIRE_PAIN:
            case type::STING_PAIN: case type::SFX_BACKGROUND: case type::ART_WALL: case type::PUSHING_BLOCK:
                return true;
            default: return false;
        }
    }

    ::pos check_tile(lua_State *L, ::world &w, int index)
    {
        const lua_Integer x = luaL_checkinteger(L, index), y = luaL_checkinteger(L, index + 1);
        const ::pos at{static_cast<int>(std::clamp<lua_Integer>(x, -1, 1000)), static_cast<int>(std::clamp<lua_Integer>(y, -1, 1000))};
        if (!valid_tile(w, at)) luaL_error(L, "%d, %d is outside the world", static_cast<int>(x), static_cast<int>(y));
        return at;
    }

    int w_getblock(lua_State *L)
    {
        ::world &w = check_world(L);
        const ::pos at = check_tile(L, w, 2);
        lua_pushinteger(L, w.blocks[cord(at.x_int(), at.y_int())].fg);
        return 1;
    }

    int w_getbackground(lua_State *L)
    {
        ::world &w = check_world(L);
        const ::pos at = check_tile(L, w, 2);
        lua_pushinteger(L, w.blocks[cord(at.x_int(), at.y_int())].bg);
        return 1;
    }

    int set_tile(lua_State *L, bool background)
    {
        ::world &w = check_world(L);
        const ::pos at = check_tile(L, w, 2);
        const lua_Integer raw = luaL_checkinteger(L, 4);
        if (raw < 0 || raw >= static_cast<lua_Integer>(items.size()) || raw > 32767) return luaL_error(L, "there is no item %d", static_cast<int>(raw));
        const short id = static_cast<short>(raw);
        ::block &b = w.blocks[cord(at.x_int(), at.y_int())];
        if (background)
        {
            if (id != 0 && id_to_item(id).type != type::BACKGROUND) return luaL_error(L, "%s is not a background", id_to_item(id).raw_name.c_str());
            b.bg = id;
            b.hits[1] = 0;
        }
        else
        {
            if (!plain_block(b.fg)) return luaL_error(L, "scripts can't replace %s (it has settings of its own)", id_to_item(b.fg).raw_name.c_str());
            if (!plain_block(id)) return luaL_error(L, "scripts can only place plain blocks, not %s", id_to_item(id).raw_name.c_str());
            if (id != 0 && id_to_item(id).type == type::BACKGROUND) return luaL_error(L, "%s is a background: use setBackground", id_to_item(id).raw_name.c_str());
            b.fg = id;
            b.hits[0] = 0;
        }
        world_tile_update(w, at);
        return 0;
    }
    int w_setblock(lua_State *L) { return set_tile(L, false); }
    int w_setbackground(lua_State *L) { return set_tile(L, true); }

    int w_players(lua_State *L)
    {
        ::world &w = check_world(L);
        lua_newtable(L);
        int i = 0;
        peers(w.name, PeerSearch::WORLD, [&](ENetPeer *p)
        {
            const ::peer *pp = static_cast<::peer*>(p->data);
            if (pp == nullptr || !pp->authenticated) return;
            push_player(L, p);
            lua_rawseti(L, -2, ++i);
        });
        return 1;
    }

    int w_message(lua_State *L)
    {
        ::world &w = check_world(L);
        const std::string text = check_text(L, 2);
        peers(w.name, PeerSearch::WORLD, [&](ENetPeer *p) { on::ConsoleMessage(p, text); });
        return 0;
    }

    int w_overlay(lua_State *L)
    {
        ::world &w = check_world(L);
        const std::string text = check_text(L, 2);
        peers(w.name, PeerSearch::WORLD, [&](ENetPeer *p) { send_varlist(p, { "OnTextOverlay", text }); });
        return 0;
    }

    int w_sound(lua_State *L)
    {
        ::world &w = check_world(L);
        const std::string file = luaL_checkstring(L, 2);
        if (file.empty() || file.size() > 64 || file.find("..") != std::string::npos ||
            file.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_/.-") != std::string::npos)
            return luaL_error(L, "a sound is a file like audio/success.wav");
        peers(w.name, PeerSearch::WORLD, [&](ENetPeer *p) { play_sfx(p, file); });
        return 0;
    }

    int w_drop(lua_State *L)
    {
        ::world &w = check_world(L);
        const ::pos at = check_tile(L, w, 2);
        const short id = check_item(L, 4);
        const int count = check_count(L, 5);
        ENetPeer *someone = nullptr;
        peers(w.name, PeerSearch::WORLD, [&](ENetPeer *p) { if (someone == nullptr) someone = p; });
        if (someone == nullptr) return 0;
        ENetEvent ev{}; ev.peer = someone;
        add_drop(ev, ::slot(id, static_cast<short>(count)), at.by_32(false), w);
        return 0;
    }

    int w_index(lua_State *L)
    {
        auto *ud = static_cast<ud_world*>(luaL_checkudata(L, 1, "psnode.world"));
        const char *key = luaL_checkstring(L, 2);
        luaL_getmetatable(L, "psnode.world");
        lua_getfield(L, -1, "methods");
        lua_getfield(L, -1, key);
        if (!lua_isnil(L, -1)) return 1;
        lua_pop(L, 3);

        const std::string_view k = key;
        if (k == "name") { lua_pushstring(L, ud->name); return 1; }
        ::world *w = world_by_name(ud->name);
        if (k == "open") { lua_pushboolean(L, w != nullptr); return 1; }
        if (w == nullptr) return luaL_error(L, "the world %s is not open (nobody is in it)", ud->name);
        if (k == "width") { lua_pushinteger(L, 100); return 1; }
        if (k == "height") { lua_pushinteger(L, static_cast<lua_Integer>(w->blocks.size() / 100)); return 1; }
        if (k == "ownerId") { lua_pushinteger(L, w->owner); return 1; }
        if (k == "playerCount") { lua_pushinteger(L, w->visitors); return 1; }
        lua_pushnil(L);
        return 1;
    }

    int w_eq(lua_State *L)
    {
        auto *a = static_cast<ud_world*>(luaL_testudata(L, 1, "psnode.world"));
        auto *b = static_cast<ud_world*>(luaL_testudata(L, 2, "psnode.world"));
        lua_pushboolean(L, a && b && std::strcmp(a->name, b->name) == 0);
        return 1;
    }

    int w_tostring(lua_State *L)
    {
        auto *ud = static_cast<ud_world*>(luaL_checkudata(L, 1, "psnode.world"));
        lua_pushstring(L, ud->name);
        return 1;
    }

    /* ---------- server, items, timers, events ---------- */

    int s_players(lua_State *L)
    {
        lua_newtable(L);
        int i = 0;
        peers("", PeerSearch::EVERYONE, [&](ENetPeer *p)
        {
            const ::peer *pp = static_cast<::peer*>(p->data);
            if (pp == nullptr || !pp->authenticated) return;
            push_player(L, p);
            lua_rawseti(L, -2, ++i);
        });
        return 1;
    }

    int s_player(lua_State *L)
    {
        ENetPeer *p = moderation::find_online(luaL_checkstring(L, 1));
        if (p == nullptr) lua_pushnil(L);
        else push_player(L, p);
        return 1;
    }

    int s_world(lua_State *L)
    {
        std::string name = luaL_checkstring(L, 1);
        for (char &c : name) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (world_by_name(name) == nullptr) lua_pushnil(L);
        else push_world(L, name);
        return 1;
    }

    int s_broadcast(lua_State *L)
    {
        const std::string text = check_text(L, 1);
        peers("", PeerSearch::EVERYONE, [&](ENetPeer *p)
        {
            const ::peer *pp = static_cast<::peer*>(p->data);
            if (pp != nullptr && pp->authenticated) on::ConsoleMessage(p, text);
        });
        return 0;
    }

    int s_online(lua_State *L)
    {
        int n = 0;
        peers("", PeerSearch::EVERYONE, [&](ENetPeer *p)
        {
            const ::peer *pp = static_cast<::peer*>(p->data);
            if (pp != nullptr && pp->authenticated) ++n;
        });
        lua_pushinteger(L, n);
        return 1;
    }

    int s_get(lua_State *L) { return store_get(L, "", 1); }
    int s_set(lua_State *L) { return store_set(L, "", 1); }

    int i_name(lua_State *L)
    {
        const lua_Integer id = luaL_checkinteger(L, 1);
        if (id < 0 || id >= static_cast<lua_Integer>(items.size())) lua_pushnil(L);
        else lua_pushstring(L, items[id].raw_name.c_str());
        return 1;
    }

    int i_rarity(lua_State *L)
    {
        lua_pushinteger(L, id_to_item(check_item(L, 1)).rarity);
        return 1;
    }

    int i_find(lua_State *L)
    {
        const std::string wanted = lower(luaL_checkstring(L, 1));
        for (const ::item &it : items)
            if (it.id != 0 && lower(it.raw_name) == wanted) { lua_pushinteger(L, it.id); return 1; }
        lua_pushnil(L);
        return 1;
    }

    int i_count(lua_State *L)
    {
        lua_pushinteger(L, static_cast<lua_Integer>(items.size()));
        return 1;
    }

    int l_on(lua_State *L)
    {
        const std::string name = luaL_checkstring(L, 1);
        luaL_checktype(L, 2, LUA_TFUNCTION);
        for (int i = 0; i < EV_COUNT; ++i)
        {
            if (name != EVENT_NAMES[i]) continue;
            if (E.handlers[i].size() >= 50) return luaL_error(L, "too many functions for the %s event", name.c_str());
            lua_pushvalue(L, 2);
            E.handlers[i].push_back({ luaL_ref(L, LUA_REGISTRYINDEX), E.script });
            return 0;
        }
        std::string all{};
        for (const char *ev : EVENT_NAMES) all += std::format("{}{}", all.empty() ? "" : ", ", ev);
        return luaL_error(L, "there is no event called '%s'. the events are: %s", name.c_str(), all.c_str());
    }

    int l_command(lua_State *L)
    {
        std::string name = lower(luaL_checkstring(L, 1));
        if (!name.starts_with('/')) name = "/" + name;
        luaL_checktype(L, 2, LUA_TFUNCTION);
        const std::string description = lua_isnoneornil(L, 3) ? "" : check_text(L, 3, 100);
        u_char role = PLAYER;
        if (!lua_isnoneornil(L, 4))
        {
            const std::string r = lower(luaL_checkstring(L, 4));
            if (r == "moderator" || r == "staff" || r == "mod") role = MODERATOR;
            else if (r == "developer" || r == "dev" || r == "owner") role = DEVELOPER;
            else if (r != "player" && r != "everyone") return luaL_error(L, "who may use it: 'player', 'moderator' or 'developer'");
        }
        if (name.size() < 2 || name.size() > 25 || name.find_first_not_of("/abcdefghijklmnopqrstuvwxyz0123456789_", 1) != std::string::npos || name.find('/', 1) != std::string::npos)
            return luaL_error(L, "a command name is 1 to 24 letters, digits or _ (like /daily)");
        if (cmd_pool.contains(name)) return luaL_error(L, "the game already has %s", name.c_str());
        if (!E.commands.contains(name) && E.commands.size() >= MAX_COMMANDS) return luaL_error(L, "too many commands");
        if (auto it = E.commands.find(name); it != E.commands.end()) luaL_unref(L, LUA_REGISTRYINDEX, it->second.ref);
        lua_pushvalue(L, 2);
        E.commands[name] = { luaL_ref(L, LUA_REGISTRYINDEX), E.script, description, role };
        return 0;
    }

    int add_timer(lua_State *L, bool repeat)
    {
        const lua_Number seconds = luaL_checknumber(L, 1);
        luaL_checktype(L, 2, LUA_TFUNCTION);
        if (!(seconds >= (repeat ? 0.1 : 0.0)) || seconds > 7 * 86400) return luaL_error(L, repeat ? "every() takes 0.1 to 604800 seconds" : "after() takes 0 to 604800 seconds");
        if (E.timers.size() >= MAX_TIMERS) return luaL_error(L, "too many timers (at most %d). cancel() the ones you don't need", static_cast<int>(MAX_TIMERS));
        lua_pushvalue(L, 2);
        const long long ms = static_cast<long long>(seconds * 1000.0);
        const int id = E.next_timer++;
        E.timers.push_back({ id, luaL_ref(L, LUA_REGISTRYINDEX), E.script, now_ms() + ms, repeat ? std::max(100LL, ms) : 0 });
        lua_pushinteger(L, id);
        return 1;
    }
    int l_after(lua_State *L) { return add_timer(L, false); }
    int l_every(lua_State *L) { return add_timer(L, true); }

    int l_cancel(lua_State *L)
    {
        const lua_Integer id = luaL_checkinteger(L, 1);
        auto it = std::ranges::find(E.timers, static_cast<int>(id), &script_timer::id);
        if (it != E.timers.end())
        {
            luaL_unref(L, LUA_REGISTRYINDEX, it->ref);
            E.timers.erase(it);
        }
        return 0;
    }

    int l_print(lua_State *L)
    {
        std::string line{};
        const int n = lua_gettop(L);
        for (int i = 1; i <= n && line.size() < 1000; ++i)
        {
            std::size_t len = 0;
            const char *s = luaL_tolstring(L, i, &len);
            if (i > 1) line += ' ';
            line.append(s, len);
            lua_pop(L, 1);
        }
        script_log(E.script, "info", std::move(line));
        return 0;
    }

    /* @note pcall and xpcall that can't catch "out of time", or a script could keep itself running forever */
    int finish_pcall(lua_State *L, int status, lua_KContext extra)
    {
        if (status != LUA_OK && status != LUA_YIELD)
        {
            if (E.killed) return lua_error(L);
            lua_pushboolean(L, 0);
            lua_pushvalue(L, -2);
            return 2;
        }
        return lua_gettop(L) - static_cast<int>(extra);
    }

    int l_pcall(lua_State *L)
    {
        luaL_checkany(L, 1);
        lua_pushboolean(L, 1);
        lua_insert(L, 1);
        const int status = lua_pcallk(L, lua_gettop(L) - 2, LUA_MULTRET, 0, 0, finish_pcall);
        return finish_pcall(L, status, 0);
    }

    int l_xpcall(lua_State *L)
    {
        const int n = lua_gettop(L);
        luaL_checktype(L, 2, LUA_TFUNCTION);
        lua_pushboolean(L, 1);
        lua_pushvalue(L, 1);
        lua_rotate(L, 3, 2);
        const int status = lua_pcallk(L, n - 2, LUA_MULTRET, 2, 2, finish_pcall);
        return finish_pcall(L, status, 2);
    }

    /* @note coroutine.resume and close catch errors too */
    int after_check(lua_State *L)
    {
        const int n = lua_gettop(L);
        lua_pushvalue(L, lua_upvalueindex(1));
        lua_insert(L, 1);
        lua_call(L, n, LUA_MULTRET);
        if (E.killed) return luaL_error(L, "the script took too long and was stopped");
        return lua_gettop(L);
    }

    /* @note finalizers (__gc) run with the time limit switched off, so scripts can't have them */
    int l_setmetatable(lua_State *L)
    {
        luaL_checktype(L, 1, LUA_TTABLE);
        if (lua_type(L, 2) == LUA_TTABLE)
        {
            lua_getfield(L, 2, "__gc");
            const bool gc = !lua_isnil(L, -1);
            lua_pop(L, 1);
            if (gc) return luaL_error(L, "__gc can't be used in server scripts");
        }
        lua_pushvalue(L, lua_upvalueindex(1));
        lua_insert(L, 1);
        lua_call(L, lua_gettop(L) - 1, 1);
        return 1;
    }

    int l_collectgarbage(lua_State *L)
    {
        lua_pushnumber(L, static_cast<lua_Number>(E.used) / 1024.0); // @note only "count": how many KB scripts use
        return 1;
    }

    int l_time(lua_State *L)
    {
        lua_pushinteger(L, static_cast<lua_Integer>(std::time(nullptr)));
        return 1;
    }

    int l_clock(lua_State *L)
    {
        lua_pushnumber(L, static_cast<lua_Number>(now_ms()) / 1000.0);
        return 1;
    }

    /* @note os.date with only the safe % codes */
    int l_date(lua_State *L)
    {
        const std::string format = luaL_optstring(L, 1, "%Y-%m-%d %H:%M:%S");
        if (format.size() > 64) return luaL_error(L, "the date format is too long");
        for (std::size_t i = 0; i < format.size(); ++i)
            if (format[i] == '%' && (++i >= format.size() || std::strchr("aAbBdHIjmMpSwyYZ%", format[i]) == nullptr))
                return luaL_error(L, "os.date can use %%a %%A %%b %%B %%d %%H %%I %%j %%m %%M %%p %%S %%w %%y %%Y %%Z");
        const std::time_t t = lua_isnoneornil(L, 2) ? std::time(nullptr) : static_cast<std::time_t>(luaL_checkinteger(L, 2));
        std::tm tm{};
#ifdef _WIN32
        gmtime_s(&tm, &t);
#else
        gmtime_r(&t, &tm);
#endif
        char out[256]{};
        const std::size_t len = std::strftime(out, sizeof(out), format.c_str(), &tm);
        lua_pushlstring(L, out, len);
        return 1;
    }

    /* ---------- running scripts ---------- */

    void *allocate(void *, void *ptr, std::size_t osize, std::size_t nsize)
    {
        const std::size_t old = ptr ? osize : 0;
        if (nsize == 0)
        {
            std::free(ptr);
            E.used -= old;
            return nullptr;
        }
        if (nsize > old && E.used + (nsize - old) > E.limit) return nullptr; // @note Lua turns this into "not enough memory"
        void *p = std::realloc(ptr, nsize);
        if (p == nullptr) return nullptr;
        E.used = E.used - old + nsize;
        return p;
    }

    void hook(lua_State *L, lua_Debug *)
    {
        if (E.killed || clock::now() > E.deadline)
        {
            E.killed = true;
            luaL_error(L, "the script took too long (over %d ms) and was stopped", static_cast<int>(E.budget.count()));
        }
    }

    int panic(lua_State *L)
    {
        const char *msg = lua_tostring(L, -1);
        throw std::runtime_error(std::format("lua: {}", msg ? msg : "unknown error"));
    }

    int traceback(lua_State *L)
    {
        const char *msg = lua_tostring(L, 1);
        if (msg == nullptr) msg = lua_pushfstring(L, "(error is a %s)", luaL_typename(L, 1));
        luaL_traceback(L, L, msg, 1);
        return 1;
    }

    using job_t = std::function<void(lua_State*)>;

    int run_job(lua_State *L)
    {
        job_t &job = *static_cast<job_t*>(lua_touserdata(L, 1));
        lua_settop(L, 0);
        std::string why{};
        try { job(L); return 0; }
        catch (const std::exception &exc) { why = exc.what(); }
        return luaL_error(L, "%s", why.c_str());
    }

    /* @note keeps the start of a traceback: the error and where it happened in the script */
    std::string short_error(std::string text)
    {
        std::string out{};
        std::size_t lines = 0;
        for (std::size_t at = 0; at < text.size() && lines < 6;)
        {
            std::size_t end = text.find('\n', at);
            if (end == std::string::npos) end = text.size();
            std::string line = text.substr(at, end - at);
            at = end + 1;
            if (line.find("[C]:") != std::string::npos || line.find("(...tail calls...)") != std::string::npos) continue;
            if (!out.empty()) out += '\n';
            out += line;
            ++lines;
        }
        return out;
    }

    /* @return false if it failed (the error is in the log). everything that touches Lua runs inside here */
    bool protect(const std::string &script, const job_t &job, std::chrono::milliseconds budget = EVENT_BUDGET)
    {
        lua_State *L = E.L;
        if (L == nullptr || E.depth >= 8) return false;
        const bool top = E.depth == 0;
        const std::string was = E.script;
        if (top)
        {
            E.budget = budget;
            E.deadline = clock::now() + budget;
            E.killed = false;
        }
        E.script = script;
        ++E.depth;
        const int base = lua_gettop(L);
        lua_pushcfunction(L, traceback);
        lua_pushcfunction(L, run_job);
        lua_pushlightuserdata(L, const_cast<job_t*>(&job));
        int status = LUA_ERRRUN;
        std::string error{};
        try
        {
            status = lua_pcall(L, 1, 0, base + 1);
            if (status != LUA_OK)
            {
                const char *msg = lua_tostring(L, -1);
                error = msg ? msg : "unknown error";
            }
        }
        catch (const std::exception &exc) // @note lua's panic: something was very wrong. stop scripts
        {
            error = exc.what();
            E.L = nullptr; // @note can't be trusted any more. it leaks, but the server keeps running
            script_log("(server)", "error", "scripts were stopped after an internal error. save a script to start them again.");
        }
        if (E.L != nullptr) lua_settop(L, base);
        --E.depth;
        E.script = was;
        if (top) E.killed = false;
        if (status != LUA_OK) script_log(script, "error", short_error(error));
        return status == LUA_OK;
    }

    void register_type(lua_State *L, const char *name, const luaL_Reg *methods, lua_CFunction index, lua_CFunction eq, lua_CFunction tostring)
    {
        luaL_newmetatable(L, name);
        lua_newtable(L);
        luaL_setfuncs(L, methods, 0);
        lua_setfield(L, -2, "methods");
        lua_pushcfunction(L, index); lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, eq); lua_setfield(L, -2, "__eq");
        lua_pushcfunction(L, tostring); lua_setfield(L, -2, "__tostring");
        lua_pushliteral(L, "locked"); lua_setfield(L, -2, "__metatable");
        lua_pop(L, 1);
    }

    void set_global(lua_State *L, const char *table, const char *field, lua_CFunction fn)
    {
        lua_getglobal(L, table);
        lua_pushcfunction(L, fn);
        lua_setfield(L, -2, field);
        lua_pop(L, 1);
    }

    void clear_field(lua_State *L, const char *table, const char *field)
    {
        lua_getglobal(L, table);
        if (lua_istable(L, -1)) { lua_pushnil(L); lua_setfield(L, -2, field); }
        lua_pop(L, 1);
    }

    /* @note helpers written in Lua: dialogs, split. loaded before the owner's scripts */
    constexpr const char *PRELUDE = R"lua(
local function clean(text)
  text = tostring(text == nil and "" or text)
  return (text:gsub("[|\r\n]", " "))
end
local function ident(text)
  text = tostring(text or "")
  if not text:match("^[%w_]+$") then error("a button or box name can only use letters, digits and _ (got '" .. text .. "')", 3) end
  return text
end

function split(text, sep)
  local words = {}
  for word in tostring(text or ""):gmatch(sep and ("[^" .. sep .. "]+") or "%S+") do words[#words + 1] = word end
  return words
end

local open = {} -- the dialog each player has open, and what to do when they press a button
local shown = 0

Dialog = {}
Dialog.__index = Dialog
setmetatable(Dialog, { __call = function(_, title, icon) return Dialog.new(title, icon) end })

function Dialog.new(title, icon)
  local d = setmetatable({ lines = { "set_default_color|`o" } }, Dialog)
  if title then d:title(title, icon) end
  return d
end
local function add(d, line) d.lines[#d.lines + 1] = line return d end
function Dialog:title(text, icon) return add(self, ("add_label_with_icon|big|%s|left|%d|"):format(clean(text), math.tointeger(icon) or 0)) end
function Dialog:text(text) return add(self, ("add_textbox|%s|left|"):format(clean(text))) end
function Dialog:small(text) return add(self, ("add_smalltext|%s|left|"):format(clean(text))) end
function Dialog:item(icon, text) return add(self, ("add_label_with_icon|small|%s|left|%d|"):format(clean(text), math.tointeger(icon) or 0)) end
function Dialog:button(id, text) return add(self, ("add_button|%s|%s|noflags|0|0|"):format(ident(id), clean(text or id))) end
function Dialog:input(id, label, default, max) return add(self, ("add_text_input|%s|%s|%s|%d|"):format(ident(id), clean(label), clean(default), math.tointeger(max) or 30)) end
function Dialog:checkbox(id, label, checked) return add(self, ("add_checkbox|%s|%s|%d|"):format(ident(id), clean(label), checked and 1 or 0)) end
function Dialog:spacer() return add(self, "add_spacer|small|") end
function Dialog:show(player, onButton, close, ok)
  shown = shown + 1
  local name = "d" .. shown
  local text = table.concat(self.lines, "\n") .. ("\nend_dialog|lua_%s|%s|%s|\n"):format(name, clean(close or "Close"), clean(ok or ""))
  player:dialog(text)
  open[player.id] = { name = name, fn = onButton }
end

on("dialog", function(player, name, button, values)
  local d = open[player.id]
  if d and d.name == name then
    open[player.id] = nil
    if d.fn then d.fn(player, button, values) end
  end
end)
on("leave", function(player) open[player.id] = nil end)
)lua";

    bool create_state()
    {
        E.used = 0;
        E.limit = std::max<std::size_t>(lua::memory_limit_mb, 8) * 1024 * 1024;
        lua_State *L = lua_newstate(allocate, nullptr);
        if (L == nullptr) return false;
        lua_atpanic(L, panic);
        E.L = L;
        E.script = "(server)";

        const job_t setup = [](lua_State *L)
        {
            luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
            luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
            luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
            luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
            luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1);
            luaL_requiref(L, LUA_COLIBNAME, luaopen_coroutine, 1);
            lua_settop(L, 0);

            /* @note take away anything that reaches outside the sandbox */
            for (const char *name : { "dofile", "loadfile", "load", "require", "warn", "collectgarbage" })
            {
                lua_pushnil(L);
                lua_setglobal(L, name);
            }
            clear_field(L, "string", "dump");
            lua_pushcfunction(L, l_collectgarbage); lua_setglobal(L, "collectgarbage");
            lua_pushcfunction(L, l_print); lua_setglobal(L, "print");
            lua_pushcfunction(L, l_print); lua_setglobal(L, "log");
            lua_pushcfunction(L, l_pcall); lua_setglobal(L, "pcall");
            lua_pushcfunction(L, l_xpcall); lua_setglobal(L, "xpcall");
            lua_getglobal(L, "setmetatable");
            lua_pushcclosure(L, l_setmetatable, 1);
            lua_setglobal(L, "setmetatable");
            for (const char *name : { "resume", "close" })
            {
                lua_getglobal(L, "coroutine");
                lua_getfield(L, -1, name);
                lua_pushcclosure(L, after_check, 1);
                lua_setfield(L, -2, name);
                lua_pop(L, 1);
            }
            lua_newtable(L);
            lua_pushcfunction(L, l_time); lua_setfield(L, -2, "time");
            lua_pushcfunction(L, l_clock); lua_setfield(L, -2, "clock");
            lua_pushcfunction(L, l_date); lua_setfield(L, -2, "date");
            lua_setglobal(L, "os");

            static const luaL_Reg player_methods[] = {
                {"message", guard<p_message>}, {"bubble", guard<p_bubble>}, {"say", guard<p_say>}, {"overlay", guard<p_overlay>},
                {"give", guard<p_give>}, {"take", guard<p_take>}, {"count", guard<p_count>}, {"has", guard<p_has>}, {"inventory", guard<p_inventory>},
                {"addGems", guard<p_addgems>}, {"setGems", guard<p_setgems>}, {"addXp", guard<p_addxp>},
                {"warp", guard<p_warp>}, {"teleport", guard<p_teleport>}, {"kick", guard<p_kick>}, {"sound", guard<p_sound>}, {"dialog", guard<p_dialog>},
                {"get", guard<p_get>}, {"set", guard<p_set>}, {"isStaff", guard<p_isstaff>}, {"isDeveloper", guard<p_isdeveloper>},
                {nullptr, nullptr}
            };
            register_type(L, "psnode.player", player_methods, guard<p_index>, p_eq, p_tostring);
            static const luaL_Reg world_methods[] = {
                {"getBlock", guard<w_getblock>}, {"getBackground", guard<w_getbackground>},
                {"setBlock", guard<w_setblock>}, {"setBackground", guard<w_setbackground>},
                {"players", guard<w_players>}, {"message", guard<w_message>}, {"overlay", guard<w_overlay>},
                {"sound", guard<w_sound>}, {"drop", guard<w_drop>},
                {nullptr, nullptr}
            };
            register_type(L, "psnode.world", world_methods, guard<w_index>, w_eq, w_tostring);

            static const luaL_Reg server_fns[] = {
                {"players", guard<s_players>}, {"player", guard<s_player>}, {"world", guard<s_world>},
                {"broadcast", guard<s_broadcast>}, {"online", guard<s_online>}, {"get", guard<s_get>}, {"set", guard<s_set>},
                {nullptr, nullptr}
            };
            luaL_newlib(L, server_fns);
            lua_pushstring(L, platform::server_name.empty() ? "PSNode" : platform::server_name.c_str());
            lua_setfield(L, -2, "name");
            lua_setglobal(L, "server");

            static const luaL_Reg item_fns[] = {
                {"name", guard<i_name>}, {"rarity", guard<i_rarity>}, {"find", guard<i_find>}, {"count", guard<i_count>},
                {nullptr, nullptr}
            };
            luaL_newlib(L, item_fns);
            lua_setglobal(L, "item");

            lua_pushcfunction(L, guard<l_on>); lua_setglobal(L, "on");
            lua_pushcfunction(L, guard<l_command>); lua_setglobal(L, "command");
            lua_pushcfunction(L, guard<l_after>); lua_setglobal(L, "after");
            lua_pushcfunction(L, guard<l_every>); lua_setglobal(L, "every");
            lua_pushcfunction(L, guard<l_cancel>); lua_setglobal(L, "cancel");

            if (luaL_loadbufferx(L, PRELUDE, std::strlen(PRELUDE), "=(built-in)", "t") != LUA_OK) lua_error(L);
            lua_call(L, 0, 0);
        };
        if (!protect("(built-in)", setup, LOAD_BUDGET)) return false;
        if (E.L != nullptr) lua_sethook(E.L, hook, LUA_MASKCOUNT, HOOK_EVERY);
        return E.L != nullptr;
    }

    void close_state()
    {
        if (E.L != nullptr) lua_close(E.L);
        E.L = nullptr;
        for (auto &list : E.handlers) list.clear();
        E.commands.clear();
        E.timers.clear();
        E.loaded.clear();
        E.depth = 0;
        E.killed = false;
    }

    void load_scripts()
    {
        if (!tables_ready || mysql_query(db.mysql, "SELECT name, source FROM lua_scripts WHERE enabled = 1 ORDER BY sort, id")) return;
        MYSQL_RES *res = mysql_store_result(db.mysql);
        if (res == nullptr) return;
        std::vector<std::pair<std::string, std::string>> scripts{};
        while (MYSQL_ROW row = mysql_fetch_row(res))
        {
            unsigned long *lengths = mysql_fetch_lengths(res);
            scripts.emplace_back(row[0] ? row[0] : "script", row[1] ? std::string(row[1], lengths[1]) : "");
        }
        mysql_free_result(res);
        if (scripts.empty()) return;

        if (!create_state())
        {
            script_log("(server)", "error", "scripts could not start");
            return;
        }
        for (const auto &[name, source] : scripts)
        {
            if (source.size() > MAX_SOURCE) { script_log(name, "error", "this script is too big (at most 256 KB)"); continue; }
            const bool ok = protect(name, [&](lua_State *L)
            {
                if (luaL_loadbufferx(L, source.data(), source.size(), ("=" + name).c_str(), "t") != LUA_OK) lua_error(L);
                lua_call(L, 0, 0);
            }, LOAD_BUDGET);
            if (ok) E.loaded.push_back(name);
            if (E.L == nullptr) return;
        }
        int handlers = 0;
        for (const auto &list : E.handlers) handlers += static_cast<int>(list.size());
        script_log("(server)", "info", std::format("loaded {} of {} scripts: {} events, {} commands, {} timers, {} KB",
            E.loaded.size(), scripts.size(), handlers - 2 /*@note the built-in dialog helpers*/, E.commands.size(), E.timers.size(), E.used / 1024));

        /* @note scripts reloaded while people are online: load what they saved */
        peers("", PeerSearch::EVERYONE, [](ENetPeer *p)
        {
            const ::peer *pp = static_cast<::peer*>(p->data);
            if (pp != nullptr && pp->authenticated) store(std::to_string(pp->user_id));
        });
    }

    /* @brief calls every function listening for this event. push_args pushes the arguments and returns how many
     * @return true if one of them returned true (stop / handled) */
    bool emit(event_id ev, const std::function<int(lua_State*)> &push_args, bool stoppable = false)
    {
        if (E.L == nullptr || E.handlers[ev].empty()) return false;
        const std::vector<handler> list = E.handlers[ev]; // @note a handler may add more
        bool stopped = false;
        for (const handler &h : list)
        {
            protect(h.script, [&](lua_State *L)
            {
                lua_rawgeti(L, LUA_REGISTRYINDEX, h.ref);
                const int n = push_args(L);
                lua_call(L, n, 1);
                if (stoppable && lua_toboolean(L, -1)) stopped = true;
            });
            if (E.L == nullptr) break;
            if (stopped) break;
        }
        return stopped;
    }

    ::peer *ready(ENetPeer *peer)
    {
        if (E.L == nullptr || peer == nullptr || peer->data == nullptr) return nullptr;
        ::peer *pp = static_cast<::peer*>(peer->data);
        return pp->authenticated ? pp : nullptr;
    }
}

void lua::init()
{
    if (mysql_query(db.mysql,
        "CREATE TABLE IF NOT EXISTS lua_scripts ("
        "id INT AUTO_INCREMENT PRIMARY KEY,"
        "name VARCHAR(48) NOT NULL,"
        "enabled TINYINT NOT NULL DEFAULT 1,"
        "mode VARCHAR(8) NOT NULL DEFAULT 'code',"
        "source MEDIUMTEXT,"
        "blocks MEDIUMTEXT,"
        "sort INT NOT NULL DEFAULT 0,"
        "updated_by VARCHAR(24) DEFAULT '',"
        "updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP"
    ")") ||
        mysql_query(db.mysql,
        "CREATE TABLE IF NOT EXISTS lua_log ("
        "id BIGINT AUTO_INCREMENT PRIMARY KEY,"
        "at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "script VARCHAR(48) NOT NULL,"
        "level VARCHAR(8) NOT NULL,"
        "message VARCHAR(1000) NOT NULL"
    ")") ||
        mysql_query(db.mysql,
        "CREATE TABLE IF NOT EXISTS lua_data ("
        "scope VARCHAR(24) NOT NULL,"
        "k VARCHAR(64) NOT NULL,"
        "v TEXT NOT NULL,"
        "PRIMARY KEY (scope, k)"
    ")"))
    {
        print(std::format("scripts are off: could not make their tables ({})", mysql_error(db.mysql)));
        return;
    }
    tables_ready = true;
    stores.clear();
    load_scripts();
    emit(EV_START, [](lua_State*) { return 0; });
}

void lua::reload()
{
    flush_stores();
    close_state();
    load_scripts();
    emit(EV_START, [](lua_State*) { return 0; });
    flush_log();
}

void lua::tick()
{
    if (!later.empty())
    {
        std::vector<std::function<void()>> run{};
        run.swap(later);
        for (auto &fn : run)
        {
            try { fn(); }
            catch (const std::exception &exc) { print(std::format("[lua] a script's warp or kick failed: {}", exc.what())); }
        }
    }
    if (E.L != nullptr && !E.timers.empty())
    {
        const long long now = now_ms();
        std::vector<script_timer> due{};
        for (const script_timer &t : E.timers) if (t.next <= now) due.push_back(t);
        for (const script_timer &t : due)
        {
            auto it = std::ranges::find(E.timers, t.id, &script_timer::id);
            if (it == E.timers.end()) continue; // @note an earlier timer cancelled it
            if (t.every > 0) it->next = now + t.every;
            else E.timers.erase(it);
            const bool ok = protect(t.script, [&](lua_State *L)
            {
                lua_rawgeti(L, LUA_REGISTRYINDEX, t.ref);
                lua_call(L, 0, 0);
            });
            if (E.L == nullptr) break;
            if (t.every == 0) luaL_unref(E.L, LUA_REGISTRYINDEX, t.ref); // @note done, and nothing else holds it
            else if (auto again = std::ranges::find(E.timers, t.id, &script_timer::id); again != E.timers.end()) // @note still there (it didn't cancel() itself)
            {
                again->fails = ok ? 0 : again->fails + 1;
                if (again->fails >= 5) // @note one that keeps failing stops, or it fills the log
                {
                    script_log(t.script, "warn", "a timer from every() failed 5 times in a row, so it was stopped");
                    luaL_unref(E.L, LUA_REGISTRYINDEX, again->ref);
                    E.timers.erase(again);
                }
            }
        }
    }
    const auto now = clock::now();
    if (now - last_flush >= std::chrono::seconds(5))
    {
        last_flush = now;
        flush_log();
        flush_stores();
    }
    if (now - last_trim >= std::chrono::minutes(10))
    {
        last_trim = now;
        trim_log();
    }
}

void lua::flush()
{
    flush_stores();
    flush_log();
}

void lua::shutdown()
{
    flush_stores();
    flush_log();
    close_state();
}

void lua::player_login(ENetPeer *peer)
{
    ::peer *pp = ready(peer);
    if (pp == nullptr) return;
    store(std::to_string(pp->user_id));
    emit(EV_JOIN, [peer](lua_State *L) { push_player(L, peer); return 1; });
}

void lua::player_logout(ENetPeer *peer)
{
    if (peer == nullptr || peer->data == nullptr) return;
    ::peer *pp = static_cast<::peer*>(peer->data);
    if (!pp->authenticated) return;
    if (E.L != nullptr)
    {
        leaving = peer;
        emit(EV_LEAVE, [peer](lua_State *L) { push_player(L, peer); return 1; });
        leaving = nullptr;
    }
    const std::string scope = std::to_string(pp->user_id);
    if (auto it = stores.find(scope); it != stores.end())
    {
        flush_store(scope, it->second);
        stores.erase(it);
    }
}

void lua::world_enter(ENetPeer *peer, const std::string &world)
{
    if (ready(peer) == nullptr) return;
    emit(EV_ENTER, [&](lua_State *L) { push_player(L, peer); push_world(L, world); return 2; });
}

void lua::world_leave(ENetPeer *peer, const std::string &world)
{
    if (ready(peer) == nullptr) return;
    emit(EV_EXIT, [&](lua_State *L) { push_player(L, peer); push_world(L, world); return 2; });
}

bool lua::command(ENetPeer *peer, std::string_view name, std::string_view text)
{
    ::peer *pp = ready(peer);
    if (pp == nullptr) return false;
    const std::string key = lower(std::string(name));
    auto it = E.commands.find(key);
    if (it == E.commands.end()) return false;
    if (pp->role < it->second.role) return false; // @note looks like any unknown command
    const command_t cmd = it->second;
    const std::string rest(command_arg(text));
    protect(cmd.script, [&](lua_State *L)
    {
        lua_rawgeti(L, LUA_REGISTRYINDEX, cmd.ref);
        push_player(L, peer);
        lua_pushlstring(L, rest.data(), rest.size());
        lua_newtable(L);
        int i = 0;
        for (std::size_t at = 0; at < rest.size();)
        {
            const std::size_t start = rest.find_first_not_of(' ', at);
            if (start == std::string::npos) break;
            std::size_t end = rest.find(' ', start);
            if (end == std::string::npos) end = rest.size();
            lua_pushlstring(L, rest.data() + start, end - start);
            lua_rawseti(L, -2, ++i);
            at = end;
        }
        lua_call(L, 3, 0);
    });
    return true;
}

std::vector<std::string> lua::command_names(const ::peer &peer)
{
    std::vector<std::string> out{};
    for (const auto &[name, cmd] : E.commands)
        if (peer.role >= cmd.role) out.push_back(name);
    return out;
}

bool lua::chat(ENetPeer *peer, const std::string &text)
{
    if (ready(peer) == nullptr) return false;
    return emit(EV_CHAT, [&](lua_State *L) { push_player(L, peer); lua_pushlstring(L, text.data(), text.size()); return 2; }, true);
}

bool lua::punch(ENetPeer *peer, int x, int y, short fg, short bg)
{
    if (E.handlers[EV_PUNCH].empty() || ready(peer) == nullptr) return false;
    return emit(EV_PUNCH, [&](lua_State *L)
    {
        push_player(L, peer);
        lua_pushinteger(L, x); lua_pushinteger(L, y);
        lua_pushinteger(L, fg != 0 ? fg : bg);
        return 4;
    }, true);
}

bool lua::place(ENetPeer *peer, int x, int y, short item)
{
    if (E.handlers[EV_PLACE].empty() || ready(peer) == nullptr) return false;
    return emit(EV_PLACE, [&](lua_State *L)
    {
        push_player(L, peer);
        lua_pushinteger(L, x); lua_pushinteger(L, y); lua_pushinteger(L, item);
        return 4;
    }, true);
}

bool lua::wrench(ENetPeer *peer, int x, int y, short fg)
{
    if (E.handlers[EV_WRENCH].empty() || ready(peer) == nullptr) return false;
    return emit(EV_WRENCH, [&](lua_State *L)
    {
        push_player(L, peer);
        lua_pushinteger(L, x); lua_pushinteger(L, y); lua_pushinteger(L, fg);
        return 4;
    }, true);
}

void lua::tile_broken(ENetPeer *peer, int x, int y, short item)
{
    ::peer *pp = ready(peer);
    if (E.handlers[EV_BREAK].empty() || pp == nullptr) return;
    /* @note after the game has sent the break, or a block a script puts here would be drawn over by it */
    const int uid = pp->user_id;
    later.push_back([uid, x, y, item]()
    {
        ENetPeer *p = peer_by_uid(uid);
        if (p == nullptr) return;
        emit(EV_BREAK, [&](lua_State *L)
        {
            push_player(L, p);
            lua_pushinteger(L, x); lua_pushinteger(L, y); lua_pushinteger(L, item);
            return 4;
        });
    });
}

bool lua::dialog(ENetPeer *peer, const std::string &header)
{
    const std::vector<std::string> parts = readch(header, '|');
    auto at = std::ranges::find(parts, std::string("dialog_name"));
    if (at == parts.end() || at + 1 == parts.end() || !(at + 1)->starts_with("lua_")) return false;
    if (ready(peer) == nullptr) return true;

    const std::string name = (at + 1)->substr(4);
    std::string button{};
    std::vector<std::pair<std::string, std::string>> values{};
    for (auto it = at + 2; it != parts.end() && it + 1 != parts.end(); it += 2)
    {
        if (*it == "buttonClicked") button = *(it + 1);
        else if (!it->empty() && values.size() < 64) values.emplace_back(*it, *(it + 1));
    }
    emit(EV_DIALOG, [&](lua_State *L)
    {
        push_player(L, peer);
        lua_pushstring(L, name.c_str());
        lua_pushstring(L, button.c_str());
        lua_createtable(L, 0, static_cast<int>(values.size()));
        for (const auto &[k, v] : values)
        {
            lua_pushlstring(L, v.data(), std::min<std::size_t>(v.size(), 1000));
            lua_setfield(L, -2, k.c_str());
        }
        return 4;
    });
    return true;
}
