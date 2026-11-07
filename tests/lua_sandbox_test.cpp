/*
 * Tests for the Lua sandbox (include/scripting/lua.cpp), against a local MariaDB.
 *
 *   make lua-test && ./lua_test.out mysql_login_test.txt
 *
 * The login file has the usual host|..., user|..., password|... lines. The tests use (and empty) the database luatest.
 * No game client is needed: a fake player sits in an ENet host that never connects anywhere.
 */
#include "pch.hpp"

#include "scripting/lua.hpp"
#include "database/database.hpp"

#include <chrono>
#include <cstdio>
#include <iostream>

namespace
{
    int failures = 0;

    void check(bool ok, const std::string &what)
    {
        std::printf("%s %s\n", ok ? "  ok  " : "  FAIL", what.c_str());
        if (!ok) ++failures;
        std::fflush(stdout);
    }

    std::string query_one(const std::string &sql)
    {
        if (mysql_query(db.mysql, sql.c_str())) return "(query failed: " + std::string(mysql_error(db.mysql)) + ")";
        MYSQL_RES *res = mysql_store_result(db.mysql);
        if (res == nullptr) return "";
        MYSQL_ROW row = mysql_fetch_row(res);
        std::string out = (row && row[0]) ? row[0] : "";
        mysql_free_result(res);
        return out;
    }

    /* @return what a script saved with server.set(key, text) */
    std::string saved(const std::string &key)
    {
        lua::flush();
        std::string v = query_one("SELECT v FROM lua_data WHERE scope = '' AND k = '" + key + "'");
        return v.empty() ? "" : v.substr(1);
    }

    bool logged(const std::string &needle)
    {
        lua::flush();
        return !query_one("SELECT COUNT(*) FROM lua_log WHERE message LIKE '%" + needle + "%'").starts_with('0');
    }

    void add_script(const std::string &name, const std::string &source)
    {
        std::string esc(source.size() * 2 + 1, '\0');
        esc.resize(mysql_real_escape_string(db.mysql, esc.data(), source.c_str(), source.size()));
        mysql_query(db.mysql, ("INSERT INTO lua_scripts (name, source) VALUES ('" + name + "', '" + esc + "')").c_str());
    }

    long long ms_since(std::chrono::steady_clock::time_point t)
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t).count();
    }

    template <typename F>
    long long timed(F &&fn)
    {
        const auto t = std::chrono::steady_clock::now();
        fn();
        return ms_since(t);
    }
}

int main(int argc, char *argv[])
{
    db.login_file = argc > 1 ? argv[1] : "mysql_login_test.txt";
    db.database = "luatest";
    mysql_library_init(0, nullptr, nullptr);
    enet_initialize();
    db.load_login();
    db.mysql_connect();
    for (const char *table : { "lua_scripts", "lua_log", "lua_data" })
        mysql_query(db.mysql, std::format("DROP TABLE IF EXISTS {}", table).c_str());

    ENetAddress address{ .type = ENET_ADDRESS_TYPE_IPV4, .port = 17988 };
    host = enet_host_create(ENET_ADDRESS_TYPE_IPV4, &address, 4, 2, 0, 0);
    if (host == nullptr) { std::puts("no enet host"); return 1; }

    items.resize(60);
    for (u_short i = 0; i < items.size(); ++i) { items[i].id = i; items[i].raw_name = std::format("Item {}", i); items[i].type = type::FOREGROUND; }
    items[2].raw_name = "Dirt";
    items[14].type = type::BACKGROUND;
    items[30].type = type::LOCK;

    ENetPeer *peer = &host->peers[0];
    peer->state = ENET_PEER_STATE_CONNECTED;
    auto *pp = new ::peer();
    pp->authenticated = true;
    pp->user_id = 7;
    pp->growid = "Tester";
    pp->display_growid = "`wTester``";
    pp->gems = 100;
    peer->data = pp;

    lua::init(); // @note makes the tables; no scripts yet
    add_script("basics", R"(
        on("join", function(p) p:set("joins", p:get("joins", 0) + 1); server.set("joined", p.name) end)
        command("/hello", function(p, text, words)
          p:give(2, 5)
          server.set("hello", text .. "|" .. #words .. "|" .. words[1])
        end, "Say hi")
        on("chat", function(p, text) if text:find("badword") then return true end end)
        on("punch", function(p, x, y, id) if x == 1 then return true end end)
        command("/gems", function(p) p:addGems(50); server.set("gems", tostring(p.gems)) end)
        command("/take", function(p) server.set("took", tostring(p:take(2, 3)) .. "/" .. tostring(p:count(2))) end)
        command("/staffonly", function(p) server.set("staff", "ran") end, "", "moderator")
        command("/boom", function(p) local x = nil; return x.field end)
        command("/sandbox", function()
          server.set("sandbox", table.concat({ tostring(io), tostring(os.execute), tostring(load), tostring(loadstring), tostring(dofile),
            tostring(debug), tostring(string.dump), tostring(require), tostring(package), tostring(collectgarbage("count") > 0) }, ","))
        end)
        command("/loop", function() while true do end end)
        command("/pcallloop", function() while true do pcall(function() while true do end end) end end)
        command("/xpcallloop", function() while true do xpcall(function() while true do end end, function(e) return e end) end end)
        command("/coloop", function() while true do coroutine.resume(coroutine.create(function() while true do end end)) end end)
        command("/wraploop", function() while true do pcall(coroutine.wrap(function() while true do end end)) end end)
        command("/mem", function() local t = {} for i = 1, 1e8 do t[i] = ("x"):rep(1000000) .. i end end)
        command("/bigrep", function() local s = ("x"):rep(2^31) end)
        command("/pattern", function() string.find(("a"):rep(40), ("a*"):rep(40) .. "b") end)
        command("/gc", function() setmetatable({}, { __gc = function() while true do end end }) end)
        command("/after", function() server.set("afterok", "yes") end)
        command("/dialog", function(p)
          Dialog("Shop", 2):text("pick"):input("amount", "How many?", "1", 5):button("buy", "Buy"):show(p, function(p, button, values)
            server.set("clicked", button .. ":" .. tostring(values.amount))
          end)
        end)
        command("/world", function(p)
          local w = server.world("TESTW")
          w:setBlock(3, 3, 2)
          w:setBackground(3, 3, 14)
          local ok1 = pcall(w.setBlock, w, 4, 4, 30)
          server.set("world", w:getBlock(3, 3) .. "," .. w:getBackground(3, 3) .. "," .. tostring(ok1) .. "," .. w.name .. "," .. w.height)
        end)
        count = 0
        every(0.1, function() count = count + 1; server.set("ticks", count) end)
        command("/date", function() server.set("date", os.date("%Y") .. "|" .. tostring(pcall(os.date, "%c"))) end)
        command("/types", function(p)
          server.set("types", type(p) .. "," .. tostring(p.id) .. "," .. tostring(p == server.player("tester")) .. "," .. tostring(item.find("dirt")) .. "," .. item.name(2))
        end)
        command("/save", function(p) p:set("n", 5); p:set("t", "text"); p:set("b", true); server.set("pi", 3.5) end)
        command("/badsave", function(p) p:set("x", {}) end)
        command("/gowarp", function(p) p:warp("start") end)
    )");
    add_script("syntax", "this is not lua");
    add_script("loadloop", "while true do end");
    lua::reload();

    check(logged("loaded 1 of 3 scripts"), "the good script loads, the broken ones are skipped");
    check(logged("syntax"), "a syntax error is logged with the script's name");
    check(logged("took too long"), "a script that loops forever while loading is stopped");

    lua::player_login(peer);
    check(saved("joined") == "Tester", "join event gets the player");

    lua::command(peer, "/hello", "/hello big world");
    check(saved("hello") == "big world|2|big", "commands get the text and the words");
    check(pp->have_slot(::slot(2, 0)) == 5, "player:give puts items in the backpack");
    lua::command(peer, "/take", "/take");
    check(saved("took") == "3/2", "player:take and player:count");
    lua::command(peer, "/gems", "/gems");
    check(saved("gems") == "150" && pp->gems == 150, "player:addGems");
    check(!lua::command(peer, "/staffonly", "/staffonly") && saved("staff").empty(), "a moderator command is hidden from players");
    check(!lua::command(peer, "/nothing", "/nothing"), "an unknown command is left to the game");

    check(lua::chat(peer, "this is a badword"), "chat event can hide a message");
    check(!lua::chat(peer, "hello"), "other chat goes through");
    check(lua::punch(peer, 1, 5, 2, 0) && !lua::punch(peer, 2, 5, 2, 0), "punch event can stop a punch");

    lua::command(peer, "/boom", "/boom");
    check(logged("basics:"), "a runtime error is logged with the script and line");

    lua::command(peer, "/sandbox", "/sandbox");
    check(saved("sandbox") == "nil,nil,nil,nil,nil,nil,nil,nil,nil,true", "no io, os.execute, load, debug, string.dump, require: " + saved("sandbox"));

    for (const char *cmd : { "/loop", "/pcallloop", "/xpcallloop", "/coloop", "/wraploop" })
    {
        const long long ms = timed([&] { lua::command(peer, cmd, cmd); });
        check(ms < 400, std::format("{} is stopped quickly ({} ms)", cmd, ms));
    }
    {
        const long long ms = timed([&] { lua::command(peer, "/mem", "/mem"); });
        check(ms < 1000 && logged("not enough memory") , std::format("using too much memory stops the script ({} ms)", ms));
        const long long ms2 = timed([&] { lua::command(peer, "/bigrep", "/bigrep"); });
        check(ms2 < 1000, std::format("a 2 GB string.rep is refused ({} ms)", ms2));
    }
    {
        const long long ms = timed([&] { lua::command(peer, "/pattern", "/pattern"); });
        check(ms < 2000 && logged("pattern too slow"), std::format("a slow pattern is stopped ({} ms)", ms));
    }
    lua::command(peer, "/gc", "/gc");
    check(logged("__gc"), "__gc is refused");
    lua::command(peer, "/after", "/after");
    check(saved("afterok") == "yes", "the server still runs scripts after all that");

    lua::command(peer, "/dialog", "/dialog");
    check(lua::dialog(peer, "action|dialog_return|dialog_name|lua_d1|buttonClicked|buy|amount|5|"), "a lua_ dialog is the script's");
    check(saved("clicked") == "buy:5", "Dialog:show calls back with the button and boxes: " + saved("clicked"));
    check(!lua::dialog(peer, "action|dialog_return|dialog_name|popup|buttonClicked|x|"), "other dialogs are left to the game");

    worlds.emplace_back("TESTW");
    pp->netid = 1;
    pp->recent_worlds.back() = "TESTW";
    lua::command(peer, "/world", "/world");
    check(saved("world") == "2,14,false,TESTW,60", "world get/set block, locks refused: " + saved("world"));

    const auto t = std::chrono::steady_clock::now();
    while (ms_since(t) < 550) { lua::tick(); enet_host_service(host, nullptr, 10); }
    const int ticks = std::atoi(saved("ticks").substr(0).c_str() );
    check(saved("ticks").size() > 0, "every() runs: " + saved("ticks"));
    (void)ticks;

    lua::command(peer, "/date", "/date");
    check(saved("date").starts_with("20") && saved("date").ends_with("|false"), "os.date with safe codes only: " + saved("date"));
    lua::command(peer, "/types", "/types");
    check(saved("types") == "userdata,7,true,2,Dirt", "player and item helpers: " + saved("types"));

    lua::command(peer, "/save", "/save");
    lua::flush();
    check(query_one("SELECT v FROM lua_data WHERE scope = '7' AND k = 'n'") == "i5", "player:set saves numbers");
    check(query_one("SELECT v FROM lua_data WHERE scope = '7' AND k = 'joins'") == "i1", "join count saved");
    lua::command(peer, "/badsave", "/badsave");
    check(logged("only text, numbers"), "tables can't be saved");

    lua::command(peer, "/gowarp", "/gowarp");
    check(true, "warp waits for the next tick (not run here)");

    lua::player_logout(peer);
    check(query_one("SELECT v FROM lua_data WHERE scope = '7' AND k = 't'") == "stext", "a player's values are saved when they leave");

    lua::reload(); // @note forgets commands and timers, loads again
    lua::player_login(peer);
    lua::command(peer, "/save", "/save");
    check(saved("hello") == "big world|2|big", "server values survive a reload");

    lua::shutdown();
    worlds.clear(); // @note before items goes away
    std::printf("\n%s: %d failed\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
