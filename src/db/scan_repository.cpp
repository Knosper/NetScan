#include "db/scan_repository.hpp"
#include "db/sqlite_helpers.hpp"
#include <sqlite3.h>

namespace
{
const char* scan_summary_projection_sql()
{
    return "id, target, host_discovery_only, requested_ports, port_coverage_known, status, "
           "message, command, exit_code, stderr_text, created_at, started_at, "
           "finished_at, deleted_at";
}

// Binds common scan result fields to stmt starting at idx.
// REQUIRES: caller executes within a Database-managed access scope.
void bind_result_fields(sqlite3_stmt* stmt, int idx, const ScanResult& result)
{
    sqlite3_bind_text(stmt, idx + 0, result.message.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, idx + 1, result.command.c_str(), -1, SQLITE_TRANSIENT);
    if (result.exit_code >= 0)
        sqlite3_bind_int(stmt, idx + 2, result.exit_code);
    else
        sqlite3_bind_null(stmt, idx + 2);
    sqlite3_bind_text(stmt, idx + 3, result.stderr_text.c_str(), -1, SQLITE_TRANSIENT);
}

PersistedScanSummary row_to_summary(sqlite3_stmt* stmt)
{
    // Column indexes must stay aligned with scan_summary_projection_sql().
    PersistedScanSummary s;
    s.id = sqlite3_column_int(stmt, 0);
    s.target = col_text(stmt, 1);
    s.host_discovery_only = sqlite3_column_int(stmt, 2) != 0;
    s.requested_ports = col_text(stmt, 3);
    s.port_coverage_known = sqlite3_column_int(stmt, 4) != 0;
    s.state = persisted_state_from_db(col_text(stmt, 5));
    s.message = col_text(stmt, 6);
    s.command = col_text(stmt, 7);
    if (sqlite3_column_type(stmt, 8) != SQLITE_NULL)
    {
        s.exit_code = sqlite3_column_int(stmt, 8);
        s.has_exit_code = true;
    }
    s.stderr_text = col_text(stmt, 9);
    s.created_at = col_text(stmt, 10);
    s.started_at = col_text(stmt, 11);
    s.finished_at = col_text(stmt, 12);
    s.deleted_at = col_text(stmt, 13);
    s.deleted = !s.deleted_at.empty();
    return s;
}

struct TerminalScanUpdate
{
    int                  scan_id = 0;
    PersistedScanState   state = PersistedScanState::Queued;
    const ScanResult*    result = nullptr;
    const std::string*   finished_at = nullptr;
    bool                 require_running = false;
};

const char* terminal_scan_update_sql(bool require_running)
{
    return require_running
        ? "UPDATE scans SET status=?, message=?, command=?, exit_code=?, stderr_text=?, "
          "finished_at=? WHERE id=? AND status='running' AND deleted_at IS NULL;"
        : "UPDATE scans SET status=?, message=?, command=?, exit_code=?, stderr_text=?, "
          "finished_at=? WHERE id=?;";
}

bool update_terminal_scan_status(sqlite3* h, const TerminalScanUpdate& update)
{
    Stmt stmt;
    stmt.ptr = db_prepare(h, terminal_scan_update_sql(update.require_running));
    if (!stmt.ptr)
        return false;

    sqlite3_bind_text(stmt.ptr, 1, to_db_value(update.state), -1, SQLITE_TRANSIENT);
    bind_result_fields(stmt.ptr, 2, *update.result);
    sqlite3_bind_text(stmt.ptr, 6, update.finished_at->c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.ptr, 7, update.scan_id);

    return exec_write_step_one_row(stmt.ptr, h);
}
} // namespace

ScanRepository::ScanRepository(Database& /*db*/) {}

int ScanRepository::insert_scan_queued(sqlite3* h, const ScanRequest& request)
{
    const char* sql =
        "INSERT INTO scans (target, status, host_discovery_only, requested_ports) "
        "VALUES (?, 'queued', ?, ?);";
    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (!stmt.ptr)
        return -1;

    sqlite3_bind_text(stmt.ptr, 1, request.target.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.ptr, 2, request.host_discovery_only ? 1 : 0);
    sqlite3_bind_text(stmt.ptr, 3, request.ports.c_str(), -1, SQLITE_TRANSIENT);

    if (!exec_write_step(stmt.ptr))
        return -1;

    return db_last_insert_rowid(h);
}

bool ScanRepository::mark_scan_running(sqlite3* h, int scan_id, const std::string& started_at)
{
    const char* sql =
        "UPDATE scans SET status='running', started_at=? WHERE id=? AND status='queued';";
    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (!stmt.ptr)
        return false;

    sqlite3_bind_text(stmt.ptr, 1, started_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.ptr, 2, scan_id);
    return exec_write_step_one_row(stmt.ptr, h);
}

bool ScanRepository::mark_scan_aborted(sqlite3* h, int scan_id, const ScanResult& result,
                                        const std::string& finished_at)
{
    return update_terminal_scan_status(
        h, TerminalScanUpdate{scan_id, PersistedScanState::Aborted, &result, &finished_at, true});
}

bool ScanRepository::mark_scan_completed(sqlite3* h, int scan_id, const ScanResult& result,
                                          const std::string& finished_at)
{
    return update_terminal_scan_status(
        h, TerminalScanUpdate{scan_id, PersistedScanState::Completed, &result, &finished_at, true});
}

bool ScanRepository::mark_scan_failed(sqlite3* h, int scan_id, const ScanResult& result,
                                       const std::string& finished_at, bool require_running)
{
    return update_terminal_scan_status(
        h, TerminalScanUpdate{scan_id, PersistedScanState::Failed, &result, &finished_at, require_running});
}

bool ScanRepository::mark_scan_dependency_missing(sqlite3* h, int scan_id,
                                                    const ScanResult& result,
                                                    const std::string& finished_at)
{
    return update_terminal_scan_status(
        h, TerminalScanUpdate{scan_id, PersistedScanState::DependencyMissing, &result,
                               &finished_at, true});
}

int ScanRepository::abort_incomplete_scans(sqlite3* h, const std::string& message,
                                            const std::string& finished_at)
{
    const char* sql =
        "UPDATE scans SET status='aborted', message=?, finished_at=? "
        "WHERE status IN ('queued', 'running');";
    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (!stmt.ptr)
        return -1;

    sqlite3_bind_text(stmt.ptr, 1, message.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.ptr, 2, finished_at.c_str(), -1, SQLITE_TRANSIENT);

    if (!exec_write_step(stmt.ptr))
        return -1;

    return db_changes(h);
}

std::unique_ptr<PersistedScanSummary> ScanRepository::get_scan_by_id(sqlite3* h, int scan_id)
{
    const std::string sql = std::string("SELECT ") + scan_summary_projection_sql() +
                            " FROM scans WHERE id=?;";
    Stmt stmt;
    stmt.ptr = db_prepare(h, sql.c_str());
    if (!stmt.ptr)
        return nullptr;

    sqlite3_bind_int(stmt.ptr, 1, scan_id);

    std::unique_ptr<PersistedScanSummary> result;
    if (sqlite3_step(stmt.ptr) == SQLITE_ROW)
        result = std::make_unique<PersistedScanSummary>(row_to_summary(stmt.ptr));
    return result;
}

bool ScanRepository::soft_delete_scan_by_id(sqlite3* h, int scan_id,
                                              const std::string& deleted_at)
{
    const char* sql = "UPDATE scans SET deleted_at=? WHERE id=? AND deleted_at IS NULL;";
    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (!stmt.ptr)
        return false;

    sqlite3_bind_text(stmt.ptr, 1, deleted_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.ptr, 2, scan_id);
    return exec_write_step_one_row(stmt.ptr, h);
}

std::unique_ptr<PersistedScanSummary> ScanRepository::get_latest_scan(sqlite3* h)
{
    const std::string sql = std::string("SELECT ") + scan_summary_projection_sql() +
                            " FROM scans WHERE deleted_at IS NULL ORDER BY id DESC LIMIT 1;";
    Stmt stmt;
    stmt.ptr = db_prepare(h, sql.c_str());
    if (!stmt.ptr)
        return nullptr;

    std::unique_ptr<PersistedScanSummary> result;
    if (sqlite3_step(stmt.ptr) == SQLITE_ROW)
        result = std::make_unique<PersistedScanSummary>(row_to_summary(stmt.ptr));
    return result;
}

bool ScanRepository::scan_exists_for_target(sqlite3* h, const std::string& target)
{
    const char* sql = "SELECT 1 FROM scans WHERE target=? AND deleted_at IS NULL LIMIT 1;";
    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (!stmt.ptr)
        return false;

    sqlite3_bind_text(stmt.ptr, 1, target.c_str(), -1, SQLITE_TRANSIENT);
    return sqlite3_step(stmt.ptr) == SQLITE_ROW;
}

std::unique_ptr<PersistedScanSummary>
ScanRepository::get_last_completed_scan_for_target(sqlite3* h, const std::string& target)
{
    const std::string sql = std::string("SELECT ") + scan_summary_projection_sql() +
                            " FROM scans WHERE target=? AND status='completed'"
                            " AND deleted_at IS NULL ORDER BY id DESC LIMIT 1;";
    Stmt stmt;
    stmt.ptr = db_prepare(h, sql.c_str());
    if (!stmt.ptr)
        return nullptr;

    sqlite3_bind_text(stmt.ptr, 1, target.c_str(), -1, SQLITE_TRANSIENT);

    std::unique_ptr<PersistedScanSummary> result;
    if (sqlite3_step(stmt.ptr) == SQLITE_ROW)
        result = std::make_unique<PersistedScanSummary>(row_to_summary(stmt.ptr));
    return result;
}

std::vector<PersistedScanSummary> ScanRepository::list_all_scans(sqlite3* h)
{
    const std::string sql = std::string("SELECT ") + scan_summary_projection_sql() +
                            " FROM scans WHERE deleted_at IS NULL ORDER BY id DESC;";
    Stmt stmt;
    stmt.ptr = db_prepare(h, sql.c_str());
    std::vector<PersistedScanSummary> rows;
    if (!stmt.ptr)
        return rows;

    while (sqlite3_step(stmt.ptr) == SQLITE_ROW)
        rows.push_back(row_to_summary(stmt.ptr));
    return rows;
}


std::string build_filtered_scans_sql(const ScanFilter& filter)
{
    std::string sql =
        "SELECT "
        "  id, target, host_discovery_only, requested_ports, port_coverage_known, status, "
        "  message, command, exit_code, stderr_text, created_at, started_at, "
        "  finished_at, deleted_at "
        "FROM scans "
        "WHERE deleted_at IS NULL "
        "  AND (? = '' OR target LIKE ? ESCAPE '\\') "
        "  AND (? = '' OR status = ?) "
        "  AND (? = '' OR created_at >= ?) "
        "  AND (? = '' OR created_at <= ?) "
        "ORDER BY id DESC";

    const bool has_limit  = filter.limit  > 0;
    const bool has_offset = filter.offset > 0;

    if (has_limit)
        sql += " LIMIT ?";
    else if (has_offset)
        sql += " LIMIT -1";

    if (has_offset)
        sql += " OFFSET ?";

    sql += ";";
    return sql;
}

int bind_filtered_scans_parameters(sqlite3_stmt* stmt, const ScanFilter& filter)
{
    int bindIdx = 1;

    sqlite3_bind_text(stmt, bindIdx++, filter.target.c_str(), -1, SQLITE_TRANSIENT);
    if (!filter.target.empty())
    {
        std::string pattern = "%" + escape_like(filter.target) + "%";
        sqlite3_bind_text(stmt, bindIdx++, pattern.c_str(), -1, SQLITE_TRANSIENT);
    }
    else
    {
        sqlite3_bind_null(stmt, bindIdx++);
    }

    sqlite3_bind_text(stmt, bindIdx++, filter.state.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, bindIdx++, filter.state.c_str(), -1, SQLITE_TRANSIENT);

    sqlite3_bind_text(stmt, bindIdx++, filter.from.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, bindIdx++, filter.from.c_str(), -1, SQLITE_TRANSIENT);

    sqlite3_bind_text(stmt, bindIdx++, filter.to.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, bindIdx++, filter.to.c_str(), -1, SQLITE_TRANSIENT);

    const bool has_limit  = filter.limit  > 0;
    const bool has_offset = filter.offset > 0;

    if (has_limit)
        sqlite3_bind_int(stmt, bindIdx++, filter.limit);
    if (has_offset)
        sqlite3_bind_int(stmt, bindIdx++, filter.offset);

    return bindIdx;
}

std::vector<PersistedScanSummary> ScanRepository::list_filtered_scans(sqlite3* h,
                                                                        const ScanFilter& filter)
{
    const std::string sql = build_filtered_scans_sql(filter);

    Stmt stmt;
    stmt.ptr = db_prepare(h, sql.c_str());
    std::vector<PersistedScanSummary> rows;
    if (!stmt.ptr)
        return rows;

    bind_filtered_scans_parameters(stmt.ptr, filter);

    while (sqlite3_step(stmt.ptr) == SQLITE_ROW)
        rows.push_back(row_to_summary(stmt.ptr));
    return rows;
}


std::unique_ptr<PersistedScanSummary>
ScanRepository::get_previous_comparable_completed_scan(sqlite3* h, int scan_id)
{
    std::unique_ptr<PersistedScanSummary> current = get_scan_by_id(h, scan_id);
    if (!current)
        return nullptr;

    const std::string sql =
        std::string("SELECT ") + scan_summary_projection_sql() + " FROM scans "
        "WHERE id < ? AND deleted_at IS NULL AND status='completed' AND target=? "
        "AND host_discovery_only=? AND requested_ports=? "
        "ORDER BY id DESC LIMIT 1;";

    Stmt stmt;
    stmt.ptr = db_prepare(h, sql.c_str());
    if (!stmt.ptr)
        return nullptr;

    sqlite3_bind_int(stmt.ptr, 1, current->id);
    sqlite3_bind_text(stmt.ptr, 2, current->target.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.ptr, 3, current->host_discovery_only ? 1 : 0);
    sqlite3_bind_text(stmt.ptr, 4, current->requested_ports.c_str(), -1, SQLITE_TRANSIENT);

    std::unique_ptr<PersistedScanSummary> result;
    if (sqlite3_step(stmt.ptr) == SQLITE_ROW)
        result = std::make_unique<PersistedScanSummary>(row_to_summary(stmt.ptr));
    return result;
}
