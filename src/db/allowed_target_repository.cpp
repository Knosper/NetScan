#include "db/allowed_target_repository.hpp"

#include "db/sqlite_helpers.hpp"

#include <sqlite3.h>

namespace
{
bool release_replace_all_savepoint(sqlite3* h)
{
    return db_exec(h, "RELEASE SAVEPOINT allowed_target_replace_all;");
}

void rollback_replace_all_savepoint(sqlite3* h)
{
    db_exec(h, "ROLLBACK TO allowed_target_replace_all;");
    db_exec(h, "RELEASE SAVEPOINT allowed_target_replace_all;");
}
} // namespace

AllowedTargetRepository::AllowedTargetRepository(Database& /*db*/) {}

std::vector<std::string> AllowedTargetRepository::list(sqlite3* h) const
{
    const char* sql = "SELECT cidr FROM user_allowed_targets ORDER BY id ASC;";

    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (!stmt.ptr)
        return {};

    std::vector<std::string> cidrs;
    while (sqlite3_step(stmt.ptr) == SQLITE_ROW)
        cidrs.push_back(col_text(stmt.ptr, 0));
    return cidrs;
}

bool AllowedTargetRepository::add(sqlite3* h, const std::string& cidr)
{
    const char* sql =
        "INSERT INTO user_allowed_targets(cidr, created_at) "
        "VALUES(?, CAST(strftime('%s', 'now') AS INTEGER));";

    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (!stmt.ptr)
        return false;

    sqlite3_bind_text(stmt.ptr, 1, cidr.c_str(), -1, SQLITE_TRANSIENT);
    return exec_write_step(stmt.ptr);
}

bool AllowedTargetRepository::remove(sqlite3* h, const std::string& cidr)
{
    const char* sql = "DELETE FROM user_allowed_targets WHERE cidr = ?;";

    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (!stmt.ptr)
        return false;

    sqlite3_bind_text(stmt.ptr, 1, cidr.c_str(), -1, SQLITE_TRANSIENT);
    if (!exec_write_step(stmt.ptr))
        return false;

    return db_changes(h) > 0;
}

bool AllowedTargetRepository::clear(sqlite3* h)
{
    return db_exec(h, "DELETE FROM user_allowed_targets;");
}

bool AllowedTargetRepository::replace_all(sqlite3* h, const std::vector<std::string>& cidrs)
{
    if (!db_exec(h, "SAVEPOINT allowed_target_replace_all;"))
        return false;

    if (!clear(h))
    {
        rollback_replace_all_savepoint(h);
        return false;
    }

    for (std::vector<std::string>::const_iterator it = cidrs.begin(); it != cidrs.end(); ++it)
    {
        if (!add(h, *it))
        {
            rollback_replace_all_savepoint(h);
            return false;
        }
    }

    if (!release_replace_all_savepoint(h))
    {
        rollback_replace_all_savepoint(h);
        return false;
    }

    return true;
}
