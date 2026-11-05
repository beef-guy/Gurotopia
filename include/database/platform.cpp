#include "pch.hpp"
#include "database.hpp"
#include "tools/terminal.hpp"

#include <algorithm>
#include <cctype>

#include "platform.hpp"

std::string platform::database{};
std::string platform::server_name{};
bool platform::gateway{false};
std::string platform::default_server{};

bool platform::enabled()
{
    return !database.empty();
}

bool platform::valid_name(const std::string &name)
{
    return !name.empty() && name.size() <= 24 &&
        std::ranges::all_of(name, [](unsigned char c) { return std::isalnum(c); });
}

void platform::create_tables()
{
    if (!enabled()) return;

    mysql_query(db.mysql, std::format("CREATE DATABASE IF NOT EXISTS `{}`", database).c_str()); // @note name is checked in main.cpp
    const std::string servers = std::format(
        "CREATE TABLE IF NOT EXISTS `{}`.servers ("
        "id INT AUTO_INCREMENT PRIMARY KEY,"
        "name VARCHAR(24) NOT NULL UNIQUE,"
        "owner_id INT DEFAULT 0,"
        "port INT NOT NULL UNIQUE,"
        "db_name VARCHAR(64) NOT NULL,"
        "max_players INT DEFAULT 50,"
        "status VARCHAR(16) DEFAULT 'stopped',"
        "online INT DEFAULT 0,"
        "last_seen TIMESTAMP NULL DEFAULT NULL,"
        "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
        ")", database);
    const std::string tickets = std::format(
        "CREATE TABLE IF NOT EXISTS `{}`.login_tickets ("
        "server_name VARCHAR(24) NOT NULL,"
        "uid INT NOT NULL,"
        "token INT NOT NULL,"
        "uuid CHAR(32) NOT NULL,"
        "expires_at DATETIME NOT NULL,"
        "PRIMARY KEY (server_name, uid)"
        ")", database);

    if (mysql_query(db.mysql, servers.c_str()) || mysql_query(db.mysql, tickets.c_str()))
        print(std::format("could not create the platform tables: {}", mysql_error(db.mysql)));
}

std::optional<platform::server> platform::find_running(const std::string &name)
{
    if (!enabled() || !valid_name(name)) return std::nullopt;

    /* @note a server that hasn't checked in for 2 minutes has probably crashed */
    ::hStmt hStmt{ std::format(
        "SELECT name, port, db_name FROM `{}`.servers "
        "WHERE name = ? AND status = 'running' AND last_seen > NOW() - INTERVAL 2 MINUTE LIMIT 1", database) };

    MYSQL_BIND param = make_bind_in(name);
    hStmt.bind_param(&param);

    platform::server server{};
    int port = 0;
    u_long lengths[2]{};
    MYSQL_BIND results[3] = { make_bind_out(server.name), make_bind_out(port), make_bind_out(server.db_name) };
    results[0].length = &lengths[0];
    results[2].length = &lengths[1];
    if (hStmt.pStmt == nullptr || mysql_stmt_bind_result(hStmt.pStmt, results)) return std::nullopt;

    hStmt.execute();
    if (mysql_stmt_fetch(hStmt.pStmt) != 0) return std::nullopt; // @note no such server, or it's offline

    server.name.resize(lengths[0]);
    server.db_name.resize(lengths[1]);
    server.port = port;
    return server;
}

void platform::report(const std::string &status, int online)
{
    if (!enabled() || server_name.empty()) return;

    ::hStmt hStmt{ std::format("UPDATE `{}`.servers SET status = ?, online = ?, last_seen = NOW() WHERE name = ?", database) };
    MYSQL_BIND params[3] = { make_bind_in(status), make_bind_in(online), make_bind_in(server_name) };
    hStmt.bind_param(params);
    hStmt.execute();

    if (hStmt.pStmt != nullptr && mysql_stmt_affected_rows(hStmt.pStmt) == 0 && status == "running")
        print(std::format("note: no row named {} in {}.servers, so the gateway can't send players here", server_name, database));
}
