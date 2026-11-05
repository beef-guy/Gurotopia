#include "pch.hpp"

#include "tools/terminal.hpp"

#include <fstream>

#include "database.hpp"

::mysql_gateway db;

void mysql_gateway::create_table_if_not_exist()
{
    int error;
    error = mysql_query(mysql,
        "CREATE TABLE IF NOT EXISTS peer ("
        "uid INT AUTO_INCREMENT PRIMARY KEY,"
        "growid VARCHAR(18) UNIQUE,"
        "password VARCHAR(128),"
        "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "inventory MEDIUMBLOB,"
        "gems BIGINT DEFAULT 0,"
        "wl_storage INT DEFAULT 0,"
        "level INT DEFAULT 1,"
        "xp INT DEFAULT 0,"
        "role INT DEFAULT 0"
    ")");
    error = mysql_query(mysql, 
        "CREATE TABLE IF NOT EXISTS world ("
        "name VARCHAR(24) NOT NULL PRIMARY KEY,"
        "owner INT DEFAULT 0,"
        "blocks MEDIUMBLOB,"
        "objects MEDIUMBLOB"
    ")");
    
    if (error) print(mysql_error(mysql));
}

bool mysql_gateway::column_found(const std::string &table, const std::string &column)
{
    mysql_query(mysql, std::format(
        "SHOW COLUMNS FROM {} WHERE Field = '{}'", 
        table, column).c_str()
    );
    MYSQL_RES *res = mysql_store_result(mysql);
    if (res == nullptr) return false; // @note the query failed

    my_ulonglong rows = mysql_num_rows(res);
    mysql_free_result(res);
    return (rows > 0);
}

int mysql_gateway::alter_table(const std::string &table, const std::string &column)
{
    return mysql_query(mysql, std::format("ALTER TABLE {} ADD COLUMN {}", table, column).c_str());
}

/* @note i use this for new columns. this could be removed once i 100% copy rgt. */
void mysql_gateway::integrity()
{
    if (!column_found("peer", "gems"))  alter_table("peer", "gems BIGINT DEFAULT 0");
    if (!column_found("peer", "level")) alter_table("peer", "level INT DEFAULT 1");
    if (!column_found("peer", "xp"))    alter_table("peer", "xp INT DEFAULT 0");
    if (!column_found("peer", "wl_storage")) alter_table("peer", "wl_storage INT DEFAULT 0");
    if (!column_found("peer", "role")) alter_table("peer", "role INT DEFAULT 0"); // @note 0 PLAYER, 1 MODERATOR, 2 DEVELOPER

    /* @note BLOB stops at 64KB, a busy world can be bigger than that */
    mysql_query(mysql, "ALTER TABLE world MODIFY blocks MEDIUMBLOB, MODIFY objects MEDIUMBLOB");
    mysql_query(mysql, "ALTER TABLE peer MODIFY inventory MEDIUMBLOB");
    mysql_query(mysql, "ALTER TABLE peer MODIFY password VARCHAR(128)"); // @note room for a hashed password
}

void mysql_gateway::mysql_connect()
{
    my_bool reconnect = 1;
    mysql_options(mysql, MYSQL_OPT_RECONNECT, &reconnect); // @note carry on if MariaDB restarts

    if (!mysql_real_connect(mysql, this->host.c_str(), this->user.c_str(), this->passwd.c_str(), NULL, 3306u, NULL, 0ul))
    {
        print(mysql_error(mysql));
        print("could not connect to MariaDB. check that it's running and that mysql_login.txt is correct.");
        fflush(stdout);
        std::exit(EXIT_FAILURE);
    }
    else print(std::format("connected to MariaDB server on {}:{}", mysql->host, mysql->port).c_str());

    mysql_query(mysql, "CREATE DATABASE IF NOT EXISTS gurotopia");
    mysql_select_db(mysql, "gurotopia");

    create_table_if_not_exist();
    integrity();
}

void mysql_gateway::load_login()
{
    std::ifstream istrm("mysql_login.txt");
    if (!istrm.is_open())
    {
        std::ofstream ostrm("mysql_login.txt");
        ostrm << 
            std::format(
                "host|{}\n"
                "user|{}\n"
                "password|{}",
                this->host, this->user, this->passwd
            );
    } // @note close ostrm
    else
    {
        for (std::string line; std::getline(istrm, line); ) 
        {
            ::hPipe hPipe{ line };

            if (!hPipe["host"].empty()) this->host = hPipe["host"];
            else if (!hPipe["user"].empty()) this->user = hPipe["user"];
            else if (!hPipe["password"].empty()) this->passwd = hPipe["password"];
        }
    } // @note delete pipes
}


hStmt::hStmt(const std::string &query)
{
    this->pStmt = mysql_stmt_init(db.mysql);
    if (!pStmt)
    {
        log_stmt_error();
        return;
    }
    if (mysql_stmt_prepare(pStmt, query.c_str(), (u_long)query.size()))
    {
        log_stmt_error();
    }
}
hStmt::~hStmt() 
{
    if (pStmt && mysql_stmt_close(pStmt))
    {
        log_stmt_error();
    }
}

void hStmt::bind_param(MYSQL_BIND *param)
{
    if (pStmt && mysql_stmt_bind_param(pStmt, param)) log_stmt_error();
}
void hStmt::execute()
{
    if (pStmt && mysql_stmt_execute(pStmt)) log_stmt_error();
}
void hStmt::fetch()
{
    if (!pStmt) return;

    int value = mysql_stmt_fetch(pStmt);
    if (value == 1 || value == MYSQL_DATA_TRUNCATED) log_stmt_error();
    else if (value == 0 || value == MYSQL_NO_DATA) /*@todo do something later...*/; // @note success
}
void hStmt::log_stmt_error() 
{
    print(pStmt ? mysql_stmt_error(pStmt) : "out of memory creating a database statement");
}
/* ~ hStmt ~ */



MYSQL_BIND make_bind_in(const signed &buffer)
{
    return { .buffer = (void*)&buffer, .buffer_type = MYSQL_TYPE_LONG };
}
MYSQL_BIND make_bind_in(const unsigned &buffer)
{
    return { .buffer = (void*)&buffer, .buffer_type = MYSQL_TYPE_LONG, .is_unsigned = true };
}
MYSQL_BIND make_bind_in(const long &buffer)
{
    return { .buffer = (void*)&buffer, .buffer_type = MYSQL_TYPE_LONG };
}
MYSQL_BIND make_bind_in(const long long &buffer)
{
    return { .buffer = (void*)&buffer, .buffer_type = MYSQL_TYPE_LONGLONG };
}
MYSQL_BIND make_bind_in(const float &buffer)
{
    return { .buffer = (void*)&buffer, .buffer_type = MYSQL_TYPE_FLOAT };
}
MYSQL_BIND make_bind_in(const std::string &buffer)
{
    return { .buffer = (void*)buffer.c_str(), .buffer_length = (u_long)buffer.size(), .buffer_type = MYSQL_TYPE_STRING };
}
MYSQL_BIND make_bind_in(const std::vector<u_char> &buffer)
{
    return { .buffer = (void*)buffer.data(), .buffer_length = (u_long)buffer.size(), .buffer_type = MYSQL_TYPE_BLOB };
}
MYSQL_BIND make_bind_in(const ::blob &buffer)
{
    return { .buffer = (void*)buffer.data().data(), .buffer_length = (u_long)buffer.size(), .buffer_type = MYSQL_TYPE_BLOB };
}

MYSQL_BIND make_bind_out(signed &buffer)
{
    return { .buffer = &buffer, .buffer_type = MYSQL_TYPE_LONG };
}
MYSQL_BIND make_bind_out(unsigned &buffer)
{
    return { .buffer = &buffer, .buffer_type = MYSQL_TYPE_LONG, .is_unsigned = true };
}
MYSQL_BIND make_bind_out(long &buffer)
{
    return { .buffer = &buffer, .buffer_type = MYSQL_TYPE_LONG };
}
MYSQL_BIND make_bind_out(long long &buffer)
{
    return { .buffer = &buffer, .buffer_type = MYSQL_TYPE_LONGLONG };
}
MYSQL_BIND make_bind_out(float &buffer)
{
    return { .buffer = &buffer, .buffer_type = MYSQL_TYPE_FLOAT };
}
MYSQL_BIND make_bind_out(std::string &buffer)
{
    buffer.resize(1024, '\0');

    return { .buffer = buffer.data(), .buffer_length = (u_long)buffer.size(), .buffer_type = MYSQL_TYPE_STRING };
}
MYSQL_BIND make_bind_out(std::vector<u_char> &buffer)
{
    buffer.resize(cord(0, 60)* sizeof(::block));

    return { .buffer = buffer.data(), .buffer_length = (u_long)buffer.size(), .buffer_type = MYSQL_TYPE_BLOB };
}
MYSQL_BIND make_bind_out(::blob &buffer)
{
    buffer.resize(2ull * 1024ull * 1024ull); // @note cut to the real length after fetch

    return { .buffer = buffer.data().data(), .buffer_length = (u_long)buffer.size(), .buffer_type = MYSQL_TYPE_BLOB };
}
