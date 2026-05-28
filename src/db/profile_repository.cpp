#include "db/profile_repository.hpp"
#include "db/sqlite_helpers.hpp"

#include <sqlite3.h>

#include <memory>
#include <vector>

namespace
{

ScanProfile read_profile_row(sqlite3_stmt* stmt)
{
    ScanProfile profile;
    profile.id                  = sqlite3_column_int(stmt, 0);
    profile.name                = col_text(stmt, 1);
    profile.target              = col_text(stmt, 2);
    profile.ports               = col_text(stmt, 3);
    profile.host_discovery_only = sqlite3_column_int(stmt, 4) != 0;
    profile.created_at          = col_text(stmt, 5);
    return profile;
}

} // namespace

ProfileRepository::ProfileRepository(Database& /*db*/)
{
}

std::vector<ScanProfile> ProfileRepository::list_profiles(sqlite3* h)
{
    std::vector<ScanProfile> rows;

    const char* sql =
        "SELECT id, name, target, ports, host_discovery_only, created_at "
        "FROM scan_profiles "
        "ORDER BY created_at DESC;";

    Stmt stmt;
    if (sqlite3_prepare_v2(h, sql, -1, &stmt.ptr, nullptr) != SQLITE_OK)
        return rows;

    while (sqlite3_step(stmt.ptr) == SQLITE_ROW)
    {
        rows.push_back(read_profile_row(stmt.ptr));
    }
    return rows;
}

std::unique_ptr<ScanProfile> ProfileRepository::get_profile(sqlite3* h, int id)
{
    const char* sql =
        "SELECT id, name, target, ports, host_discovery_only, created_at "
        "FROM scan_profiles WHERE id = ? LIMIT 1;";

    Stmt stmt;
    if (sqlite3_prepare_v2(h, sql, -1, &stmt.ptr, nullptr) != SQLITE_OK)
        return nullptr;

    sqlite3_bind_int(stmt.ptr, 1, id);

    std::unique_ptr<ScanProfile> profile;
    if (sqlite3_step(stmt.ptr) == SQLITE_ROW)
    {
        profile = std::make_unique<ScanProfile>(read_profile_row(stmt.ptr));
    }
    return profile;
}

bool ProfileRepository::profile_name_exists(sqlite3* h, const std::string& name, int exclude_id)
{
    const char* sql = exclude_id > 0
        ? "SELECT 1 FROM scan_profiles WHERE name = ? AND id != ? LIMIT 1;"
        : "SELECT 1 FROM scan_profiles WHERE name = ? LIMIT 1;";

    Stmt stmt;
    if (sqlite3_prepare_v2(h, sql, -1, &stmt.ptr, nullptr) != SQLITE_OK)
        return false;

    sqlite3_bind_text(stmt.ptr, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    if (exclude_id > 0)
        sqlite3_bind_int(stmt.ptr, 2, exclude_id);

    return sqlite3_step(stmt.ptr) == SQLITE_ROW;
}

int ProfileRepository::create_profile(sqlite3* h, const ProfileWriteRequest& req)
{
    const char* sql =
        "INSERT INTO scan_profiles (name, target, ports, host_discovery_only) "
        "VALUES (?, ?, ?, ?);";

    Stmt stmt;
    if (sqlite3_prepare_v2(h, sql, -1, &stmt.ptr, nullptr) != SQLITE_OK)
        return 0;

    sqlite3_bind_text(stmt.ptr, 1, req.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.ptr, 2, req.target.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.ptr, 3, req.ports.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.ptr, 4, req.host_discovery_only ? 1 : 0);

    int inserted_id = 0;
    if (exec_write_step(stmt.ptr))
    {
        inserted_id = static_cast<int>(sqlite3_last_insert_rowid(h));
    }
    return inserted_id;
}

bool ProfileRepository::update_profile(sqlite3* h, int id, const ProfileWriteRequest& req)
{
    const char* sql =
        "UPDATE scan_profiles "
        "SET name = ?, target = ?, ports = ?, host_discovery_only = ? "
        "WHERE id = ?;";

    Stmt stmt;
    if (sqlite3_prepare_v2(h, sql, -1, &stmt.ptr, nullptr) != SQLITE_OK)
        return false;

    sqlite3_bind_text(stmt.ptr, 1, req.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.ptr, 2, req.target.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.ptr, 3, req.ports.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.ptr, 4, req.host_discovery_only ? 1 : 0);
    sqlite3_bind_int(stmt.ptr, 5, id);

    bool success = false;
    if (sqlite3_step(stmt.ptr) == SQLITE_DONE)
        success = sqlite3_changes(h) == 1;
    if (success)
        return true;

    return get_profile(h, id) != nullptr;
}

bool ProfileRepository::delete_profile(sqlite3* h, int id)
{
    const char* sql = "DELETE FROM scan_profiles WHERE id = ?;";

    Stmt stmt;
    if (sqlite3_prepare_v2(h, sql, -1, &stmt.ptr, nullptr) != SQLITE_OK)
        return false;

    sqlite3_bind_int(stmt.ptr, 1, id);

    return exec_write_step_one_row(stmt.ptr, h);
}

bool ProfileRepository::are_default_profiles_seeded(sqlite3* h)
{
    const char* sql =
        "SELECT value FROM app_metadata WHERE key = 'default_scan_profiles_seeded' LIMIT 1;";

    Stmt stmt;
    if (sqlite3_prepare_v2(h, sql, -1, &stmt.ptr, nullptr) != SQLITE_OK)
        return false;

    bool seeded = false;
    if (sqlite3_step(stmt.ptr) == SQLITE_ROW)
        seeded = col_text(stmt.ptr, 0) == "1";
    return seeded;
}

bool ProfileRepository::mark_default_profiles_seeded(sqlite3* h)
{
    const char* sql =
        "INSERT OR REPLACE INTO app_metadata (key, value) "
        "VALUES ('default_scan_profiles_seeded', '1');";

    Stmt stmt;
    if (sqlite3_prepare_v2(h, sql, -1, &stmt.ptr, nullptr) != SQLITE_OK)
        return false;

    return exec_write_step(stmt.ptr);
}
