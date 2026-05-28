#ifndef DB_SCHEDULE_REPOSITORY_HPP
#define DB_SCHEDULE_REPOSITORY_HPP

#include "db/database.hpp"
#include <memory>
#include <string>
#include <vector>

struct ScheduledJob
{
    int         id = 0;
    std::string target;
    std::string ports;
    bool        host_discovery_only = false;
    int         interval_seconds = 0;
    bool        enabled = true;
    std::string last_run_at;
    std::string next_run_at;
};

// REQUIRES: caller passes the sqlite3* handle from the enclosing
// Database::read()/write() access scope.
class ScheduleRepository
{
public:
    explicit ScheduleRepository(Database& db);

    std::vector<ScheduledJob>         list_jobs(sqlite3* h);
    std::unique_ptr<ScheduledJob>     get_job(sqlite3* h, int id);
    int                            create_job(sqlite3* h, const ScheduledJob& job);
    bool                           delete_job(sqlite3* h, int id);
    bool                           update_job_run_times(sqlite3* h, int id,
                                                        const std::string& last_run_at,
                                                        const std::string& next_run_at);
    bool                           set_enabled(sqlite3* h, int id, bool enabled);
};

#endif
