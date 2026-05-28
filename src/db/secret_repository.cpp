#include "db/secret_repository.hpp"

#include "db/sqlite_helpers.hpp"

#include <sqlite3.h>

SecretRepository::SecretRepository(Database& /*db*/) {}

std::string SecretRepository::load_hash(sqlite3* h, const std::string& name)
{
    const char* sql = "SELECT hash FROM secrets WHERE name=?;";

    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (stmt.ptr == nullptr)
        return "";

    sqlite3_bind_text(stmt.ptr, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt.ptr) != SQLITE_ROW)
        return "";

    return col_text(stmt.ptr, 0);
}

bool SecretRepository::upsert_hash(sqlite3* h, const std::string& name, const std::string& hash)
{
    const char* sql =
        "INSERT INTO secrets(name, hash) VALUES(?, ?) "
        "ON CONFLICT(name) DO UPDATE SET hash=excluded.hash, created_at=CURRENT_TIMESTAMP;";

    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (stmt.ptr == nullptr)
        return false;

    sqlite3_bind_text(stmt.ptr, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.ptr, 2, hash.c_str(), -1, SQLITE_TRANSIENT);
    return exec_write_step(stmt.ptr);
}
