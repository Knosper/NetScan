#ifndef DB_PRESENCE_REPOSITORY_HPP
#define DB_PRESENCE_REPOSITORY_HPP

#include "db/database.hpp"

#include <memory>
#include <string>
#include <vector>

struct PresenceTracker
{
    int         id = 0;
    std::string target;
    std::string check_type;
    int         port = 0;
    std::string url;
    int         interval_seconds = 300;
    int         timeout_ms = 3000;
    bool        enabled = true;
    std::string created_at;
    std::string updated_at;
    std::string last_checked_at;
    std::string last_status;
    int         last_latency_ms = -1;
    std::string last_error;
};

struct PresenceCheckResult
{
    int         id = 0;
    int         tracker_id = 0;
    std::string status;
    int         latency_ms = -1;
    std::string error;
    std::string checked_at;
};

// REQUIRES: caller passes the sqlite3* handle from the enclosing
// Database::read()/write() access scope.
class PresenceRepository
{
public:
    explicit PresenceRepository(Database& db);

    std::vector<PresenceTracker> list_trackers(sqlite3* h);
    std::vector<PresenceTracker> list_enabled_trackers(sqlite3* h);
    std::unique_ptr<PresenceTracker> get_tracker(sqlite3* h, int id);
    int create_tracker(sqlite3* h, const PresenceTracker& tracker);
    bool update_tracker(sqlite3* h, const PresenceTracker& tracker);
    bool delete_tracker(sqlite3* h, int id);
    int count_trackers(sqlite3* h);

    int create_result(sqlite3* h, const PresenceCheckResult& result);
    std::vector<PresenceCheckResult> list_results(sqlite3* h, int tracker_id, int limit);
    int prune_results_older_than(sqlite3* h, const std::string& cutoff);
    int prune_results_exceeding_total(sqlite3* h, int keep_count);
};

#endif
