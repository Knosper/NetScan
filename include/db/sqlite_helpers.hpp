#ifndef DB_SQLITE_HELPERS_HPP
#define DB_SQLITE_HELPERS_HPP

#include "database.hpp"
#include <sqlite3.h>
#include <string>

// NOTE: All helpers below receive an explicit sqlite3* handle.
// REQUIRES: the caller executes inside a Database-managed read()/write()
// access scope, or otherwise holds the transitional Database::lock().

// Statement helpers (prepare + row access): repository-side query lifecycle.

// Returns the text value of column idx, or "" if the column is NULL.
inline std::string col_text(sqlite3_stmt* stmt, int idx)
{
    const unsigned char* p = sqlite3_column_text(stmt, idx);
    return p ? reinterpret_cast<const char*>(p) : "";
}

// Prepares a statement against h; returns nullptr on failure.
inline sqlite3_stmt* db_prepare(sqlite3* h, const char* sql)
{
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(h, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return nullptr;
    return stmt;
}

// Returns the last SQLite error message for the given handle.
inline std::string db_error_message(sqlite3* h)
{
    return sqlite3_errmsg(h);
}

// Connection-state helpers are intended for write-side repository code.
inline int db_changes(sqlite3* h)
{
    return sqlite3_changes(h);
}

inline int db_last_insert_rowid(sqlite3* h)
{
    return static_cast<int>(sqlite3_last_insert_rowid(h));
}

// Exec helper for statements that do not return result rows.
inline bool db_exec(sqlite3* h, const char* sql, std::string* error_message = nullptr)
{
    char* error = nullptr;
    const int rc = sqlite3_exec(h, sql, nullptr, nullptr, &error);
    if (rc == SQLITE_OK)
        return true;

    if (error_message != nullptr)
        *error_message = error ? error : "unknown sqlite error";
    sqlite3_free(error);
    return false;
}

// Executes one step of a prepared write statement, then resets it.
// Returns true when sqlite3_step() returns SQLITE_DONE.
inline bool finish_write_step(sqlite3_stmt* stmt)
{
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    return ok;
}

// Executes one step of a prepared write statement (no reset/clear_bindings).
// Returns true when sqlite3_step() returns SQLITE_DONE.
inline bool exec_write_step(sqlite3_stmt* stmt)
{
    return sqlite3_step(stmt) == SQLITE_DONE;
}

// Executes one step and returns true only when exactly one row was affected.
inline bool exec_write_step_one_row(sqlite3_stmt* stmt, sqlite3* h)
{
    return sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(h) == 1;
}

// Executes one step of a prepared write statement and returns sqlite3_changes().
// The step return value is intentionally discarded; the caller relies on the
// change count for success detection (fire-and-check pattern for UPDATE/DELETE).
inline int exec_write_step_changes(sqlite3_stmt* stmt, sqlite3* h)
{
    sqlite3_step(stmt);
    return db_changes(h);
}

/**
 * Escapes SQL LIKE wildcard characters %, _ and the escape character itself \
 * Use with ESCAPE '\' clause in LIKE queries
 */
inline std::string escape_like(const std::string& input)
{
    std::string result;
    result.reserve(input.size());
    for (char c : input) {
        if (c == '%' || c == '_' || c == '\\')
            result += '\\';
        result += c;
    }
    return result;
}

#endif
