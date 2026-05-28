#include "db/host_meta_repository.hpp"
#include "db/sqlite_helpers.hpp"
#include <sqlite3.h>

namespace
{
HostMeta read_meta_row(sqlite3_stmt* stmt)
{
    HostMeta meta;
    meta.display_name = col_text(stmt, 0);
    meta.role         = col_text(stmt, 1);
    meta.tags         = col_text(stmt, 2);
    return meta;
}
} // namespace

HostMetaRepository::HostMetaRepository(Database& /*db*/) {}

HostMeta HostMetaRepository::get_meta(sqlite3* h, const std::string& ip)
{
    const char* sql = "SELECT display_name, role, tags FROM host_meta WHERE ip=?;";
    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (!stmt.ptr)
        return HostMeta{};

    sqlite3_bind_text(stmt.ptr, 1, ip.c_str(), -1, SQLITE_TRANSIENT);
    HostMeta meta;
    if (sqlite3_step(stmt.ptr) == SQLITE_ROW)
        meta = read_meta_row(stmt.ptr);
    return meta;
}

void HostMetaRepository::upsert_meta(sqlite3* h, const std::string& ip, const HostMeta& meta)
{
    const char* sql =
        "INSERT INTO host_meta(ip, display_name, role, tags) VALUES(?,?,?,?) "
        "ON CONFLICT(ip) DO UPDATE SET "
        "display_name=excluded.display_name, role=excluded.role, tags=excluded.tags;";
    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (!stmt.ptr)
        return;

    sqlite3_bind_text(stmt.ptr, 1, ip.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.ptr, 2, meta.display_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.ptr, 3, meta.role.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.ptr, 4, meta.tags.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt.ptr);
}

void HostMetaRepository::clear_field(sqlite3* h, const std::string& ip,
                                      const std::string& field)
{
    const char* sql = nullptr;
    if (field == "display_name")
        sql = "UPDATE host_meta SET display_name='' WHERE ip=?;";
    else if (field == "role")
        sql = "UPDATE host_meta SET role='' WHERE ip=?;";
    else if (field == "tags")
        sql = "UPDATE host_meta SET tags='' WHERE ip=?;";
    else
        return;

    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (!stmt.ptr)
        return;

    sqlite3_bind_text(stmt.ptr, 1, ip.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt.ptr);
}
