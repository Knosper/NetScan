#include "db/presence_repository.hpp"
#include "db/sqlite_helpers.hpp"

#include <sqlite3.h>

namespace
{
PresenceTracker read_tracker_row(sqlite3_stmt* stmt)
{
    PresenceTracker tracker;
    tracker.id               = sqlite3_column_int(stmt, 0);
    tracker.target           = col_text(stmt, 1);
    tracker.check_type       = col_text(stmt, 2);
    tracker.port             = sqlite3_column_int(stmt, 3);
    tracker.url              = col_text(stmt, 4);
    tracker.interval_seconds = sqlite3_column_int(stmt, 5);
    tracker.timeout_ms       = sqlite3_column_int(stmt, 6);
    tracker.enabled          = sqlite3_column_int(stmt, 7) != 0;
    tracker.created_at       = col_text(stmt, 8);
    tracker.updated_at       = col_text(stmt, 9);
    tracker.last_checked_at  = col_text(stmt, 10);
    tracker.last_status      = col_text(stmt, 11);
    tracker.last_latency_ms  = sqlite3_column_type(stmt, 12) == SQLITE_NULL
                                   ? -1
                                   : sqlite3_column_int(stmt, 12);
    tracker.last_error       = col_text(stmt, 13);
    return tracker;
}

PresenceCheckResult read_result_row(sqlite3_stmt* stmt)
{
    PresenceCheckResult result;
    result.id         = sqlite3_column_int(stmt, 0);
    result.tracker_id = sqlite3_column_int(stmt, 1);
    result.status     = col_text(stmt, 2);
    result.latency_ms = sqlite3_column_type(stmt, 3) == SQLITE_NULL
                            ? -1
                            : sqlite3_column_int(stmt, 3);
    result.error      = col_text(stmt, 4);
    result.checked_at = col_text(stmt, 5);
    return result;
}

const char* tracker_select_sql()
{
    return "SELECT t.id, t.target, t.check_type, t.port, t.url, t.interval_seconds, "
           "t.timeout_ms, t.enabled, t.created_at, t.updated_at, r.checked_at, r.status, "
           "r.latency_ms, r.error FROM presence_trackers t "
           "LEFT JOIN presence_results r ON r.id = ("
           "SELECT id FROM presence_results WHERE tracker_id = t.id "
           "ORDER BY checked_at DESC, id DESC LIMIT 1)";
}
} // namespace

PresenceRepository::PresenceRepository(Database& /*db*/) {}

std::vector<PresenceTracker> PresenceRepository::list_trackers(sqlite3* h)
{
    std::string sql = std::string(tracker_select_sql()) + " ORDER BY t.id ASC;";
    Stmt stmt;
    stmt.ptr = db_prepare(h, sql.c_str());
    if (!stmt.ptr)
        return {};

    std::vector<PresenceTracker> trackers;
    while (sqlite3_step(stmt.ptr) == SQLITE_ROW)
        trackers.push_back(read_tracker_row(stmt.ptr));
    return trackers;
}

std::vector<PresenceTracker> PresenceRepository::list_enabled_trackers(sqlite3* h)
{
    std::string sql = std::string(tracker_select_sql()) +
                      " WHERE t.enabled = 1 ORDER BY t.id ASC;";
    Stmt stmt;
    stmt.ptr = db_prepare(h, sql.c_str());
    if (!stmt.ptr)
        return {};

    std::vector<PresenceTracker> trackers;
    while (sqlite3_step(stmt.ptr) == SQLITE_ROW)
        trackers.push_back(read_tracker_row(stmt.ptr));
    return trackers;
}

std::unique_ptr<PresenceTracker> PresenceRepository::get_tracker(sqlite3* h, int id)
{
    std::string sql = std::string(tracker_select_sql()) + " WHERE t.id = ?;";
    Stmt stmt;
    stmt.ptr = db_prepare(h, sql.c_str());
    if (!stmt.ptr)
        return nullptr;

    sqlite3_bind_int(stmt.ptr, 1, id);

    std::unique_ptr<PresenceTracker> tracker;
    if (sqlite3_step(stmt.ptr) == SQLITE_ROW)
        tracker.reset(new PresenceTracker(read_tracker_row(stmt.ptr)));
    return tracker;
}

int PresenceRepository::create_tracker(sqlite3* h, const PresenceTracker& tracker)
{
    const char* sql =
        "INSERT INTO presence_trackers(target, check_type, port, url, interval_seconds, "
        "timeout_ms, enabled, created_at, updated_at) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP);";

    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (!stmt.ptr)
        return -1;

    sqlite3_bind_text(stmt.ptr, 1, tracker.target.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.ptr, 2, tracker.check_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.ptr, 3, tracker.port);
    sqlite3_bind_text(stmt.ptr, 4, tracker.url.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.ptr, 5, tracker.interval_seconds);
    sqlite3_bind_int(stmt.ptr, 6, tracker.timeout_ms);
    sqlite3_bind_int(stmt.ptr, 7, tracker.enabled ? 1 : 0);

    const bool ok = exec_write_step(stmt.ptr);
    return ok ? db_last_insert_rowid(h) : -1;
}

bool PresenceRepository::update_tracker(sqlite3* h, const PresenceTracker& tracker)
{
    const char* sql =
        "UPDATE presence_trackers SET target = ?, check_type = ?, port = ?, url = ?, "
        "interval_seconds = ?, timeout_ms = ?, enabled = ?, updated_at = CURRENT_TIMESTAMP "
        "WHERE id = ?;";

    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (!stmt.ptr)
        return false;

    sqlite3_bind_text(stmt.ptr, 1, tracker.target.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.ptr, 2, tracker.check_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.ptr, 3, tracker.port);
    sqlite3_bind_text(stmt.ptr, 4, tracker.url.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.ptr, 5, tracker.interval_seconds);
    sqlite3_bind_int(stmt.ptr, 6, tracker.timeout_ms);
    sqlite3_bind_int(stmt.ptr, 7, tracker.enabled ? 1 : 0);
    sqlite3_bind_int(stmt.ptr, 8, tracker.id);

    return exec_write_step_changes(stmt.ptr, h) > 0;
}

bool PresenceRepository::delete_tracker(sqlite3* h, int id)
{
    const char* delete_results_sql = "DELETE FROM presence_results WHERE tracker_id = ?;";
    Stmt result_stmt;
    result_stmt.ptr = db_prepare(h, delete_results_sql);
    if (!result_stmt.ptr)
        return false;

    sqlite3_bind_int(result_stmt.ptr, 1, id);
    sqlite3_step(result_stmt.ptr);

    const char* sql = "DELETE FROM presence_trackers WHERE id = ?;";
    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (!stmt.ptr)
        return false;

    sqlite3_bind_int(stmt.ptr, 1, id);
    return exec_write_step_changes(stmt.ptr, h) > 0;
}

int PresenceRepository::count_trackers(sqlite3* h)
{
    Stmt stmt;
    stmt.ptr = db_prepare(h, "SELECT COUNT(*) FROM presence_trackers;");
    if (!stmt.ptr)
        return -1;

    int count = 0;
    if (sqlite3_step(stmt.ptr) == SQLITE_ROW)
        count = sqlite3_column_int(stmt.ptr, 0);
    return count;
}

int PresenceRepository::create_result(sqlite3* h, const PresenceCheckResult& result)
{
    const char* sql =
        "INSERT INTO presence_results(tracker_id, status, latency_ms, error, checked_at) "
        "VALUES(?, ?, ?, ?, CURRENT_TIMESTAMP);";

    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (!stmt.ptr)
        return -1;

    sqlite3_bind_int(stmt.ptr, 1, result.tracker_id);
    sqlite3_bind_text(stmt.ptr, 2, result.status.c_str(), -1, SQLITE_TRANSIENT);
    if (result.latency_ms < 0)
        sqlite3_bind_null(stmt.ptr, 3);
    else
        sqlite3_bind_int(stmt.ptr, 3, result.latency_ms);
    sqlite3_bind_text(stmt.ptr, 4, result.error.c_str(), -1, SQLITE_TRANSIENT);

    const bool ok = exec_write_step(stmt.ptr);
    return ok ? db_last_insert_rowid(h) : -1;
}

std::vector<PresenceCheckResult> PresenceRepository::list_results(sqlite3* h, int tracker_id,
                                                                    int limit)
{
    const char* sql =
        "SELECT id, tracker_id, status, latency_ms, error, checked_at "
        "FROM presence_results WHERE tracker_id = ? "
        "ORDER BY checked_at DESC, id DESC LIMIT ?;";
    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (!stmt.ptr)
        return {};

    sqlite3_bind_int(stmt.ptr, 1, tracker_id);
    sqlite3_bind_int(stmt.ptr, 2, limit);

    std::vector<PresenceCheckResult> results;
    while (sqlite3_step(stmt.ptr) == SQLITE_ROW)
        results.push_back(read_result_row(stmt.ptr));
    return results;
}

int PresenceRepository::prune_results_older_than(sqlite3* h, const std::string& cutoff)
{
    const char* sql = "DELETE FROM presence_results WHERE checked_at < ?;";
    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (!stmt.ptr)
        return 0;

    sqlite3_bind_text(stmt.ptr, 1, cutoff.c_str(), -1, SQLITE_TRANSIENT);
    return exec_write_step_changes(stmt.ptr, h);
}

int PresenceRepository::prune_results_exceeding_total(sqlite3* h, int keep_count)
{
    if (keep_count <= 0)
    {
        const char* delete_all_sql = "DELETE FROM presence_results;";
        Stmt stmt;
        stmt.ptr = db_prepare(h, delete_all_sql);
        if (!stmt.ptr)
            return 0;

        return exec_write_step_changes(stmt.ptr, h);
    }

    const char* sql =
        "DELETE FROM presence_results WHERE id IN ("
        "SELECT id FROM presence_results "
        "ORDER BY checked_at DESC, id DESC "
        "LIMIT -1 OFFSET ?);";
    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (!stmt.ptr)
        return 0;

    sqlite3_bind_int(stmt.ptr, 1, keep_count);
    return exec_write_step_changes(stmt.ptr, h);
}
