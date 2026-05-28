#include "db/schedule_repository.hpp"
#include "db/sqlite_helpers.hpp"
#include <memory>
#include <sqlite3.h>

namespace
{
ScheduledJob read_job_row(sqlite3_stmt* stmt)
{
    ScheduledJob job;
    job.id                  = sqlite3_column_int(stmt, 0);
    job.target              = col_text(stmt, 1);
    job.ports               = col_text(stmt, 2);
    job.host_discovery_only = sqlite3_column_int(stmt, 3) != 0;
    job.interval_seconds    = sqlite3_column_int(stmt, 4);
    job.enabled             = sqlite3_column_int(stmt, 5) != 0;
    job.last_run_at         = col_text(stmt, 6);
    job.next_run_at         = col_text(stmt, 7);
    return job;
}

const char* job_select_sql()
{
    return "SELECT id, target, ports, host_discovery_only, interval_seconds, "
           "enabled, last_run_at, next_run_at FROM scheduled_jobs";
}
} // namespace

ScheduleRepository::ScheduleRepository(Database& /*db*/) {}

std::vector<ScheduledJob> ScheduleRepository::list_jobs(sqlite3* h)
{
    std::string sql = std::string(job_select_sql()) + " ORDER BY id ASC;";
    Stmt stmt;
    stmt.ptr = db_prepare(h, sql.c_str());
    if (!stmt.ptr)
        return {};

    std::vector<ScheduledJob> jobs;
    while (sqlite3_step(stmt.ptr) == SQLITE_ROW)
        jobs.push_back(read_job_row(stmt.ptr));
    return jobs;
}

std::unique_ptr<ScheduledJob> ScheduleRepository::get_job(sqlite3* h, int id)
{
    std::string sql = std::string(job_select_sql()) + " WHERE id = ?;";
    Stmt stmt;
    stmt.ptr = db_prepare(h, sql.c_str());
    if (!stmt.ptr)
        return nullptr;

    sqlite3_bind_int(stmt.ptr, 1, id);

    std::unique_ptr<ScheduledJob> result;
    if (sqlite3_step(stmt.ptr) == SQLITE_ROW)
        result.reset(new ScheduledJob(read_job_row(stmt.ptr)));
    return result;
}

int ScheduleRepository::create_job(sqlite3* h, const ScheduledJob& job)
{
    const char* sql =
        "INSERT INTO scheduled_jobs(target, ports, host_discovery_only, interval_seconds, "
        "enabled, last_run_at, next_run_at) VALUES(?, ?, ?, ?, ?, ?, ?);";

    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (!stmt.ptr)
        return -1;

    sqlite3_bind_text(stmt.ptr, 1, job.target.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.ptr, 2, job.ports.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.ptr, 3, job.host_discovery_only ? 1 : 0);
    sqlite3_bind_int(stmt.ptr, 4, job.interval_seconds);
    sqlite3_bind_int(stmt.ptr, 5, job.enabled ? 1 : 0);

    if (job.last_run_at.empty())
        sqlite3_bind_null(stmt.ptr, 6);
    else
        sqlite3_bind_text(stmt.ptr, 6, job.last_run_at.c_str(), -1, SQLITE_TRANSIENT);

    if (job.next_run_at.empty())
        sqlite3_bind_null(stmt.ptr, 7);
    else
        sqlite3_bind_text(stmt.ptr, 7, job.next_run_at.c_str(), -1, SQLITE_TRANSIENT);

    const bool ok = exec_write_step(stmt.ptr);

    return ok ? db_last_insert_rowid(h) : -1;
}

bool ScheduleRepository::delete_job(sqlite3* h, int id)
{
    const char* sql = "DELETE FROM scheduled_jobs WHERE id = ?;";
    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (!stmt.ptr)
        return false;

    sqlite3_bind_int(stmt.ptr, 1, id);
    if (!exec_write_step(stmt.ptr))
        return false;

    return db_changes(h) > 0;
}

bool ScheduleRepository::update_job_run_times(sqlite3* h, int id,
                                               const std::string& last_run_at,
                                               const std::string& next_run_at)
{
    const char* sql =
        "UPDATE scheduled_jobs SET last_run_at = ?, next_run_at = ? WHERE id = ?;";
    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (!stmt.ptr)
        return false;

    sqlite3_bind_text(stmt.ptr, 1, last_run_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.ptr, 2, next_run_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.ptr, 3, id);

    if (!exec_write_step(stmt.ptr))
        return false;

    return db_changes(h) > 0;
}

bool ScheduleRepository::set_enabled(sqlite3* h, int id, bool enabled)
{
    const char* sql = "UPDATE scheduled_jobs SET enabled = ? WHERE id = ?;";
    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (!stmt.ptr)
        return false;

    sqlite3_bind_int(stmt.ptr, 1, enabled ? 1 : 0);
    sqlite3_bind_int(stmt.ptr, 2, id);

    if (!exec_write_step(stmt.ptr))
        return false;

    return db_changes(h) > 0;
}
