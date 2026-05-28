#ifndef SERVICE_PRESENCE_SERVICE_HPP
#define SERVICE_PRESENCE_SERVICE_HPP

#include "db/database.hpp"
#include "db/presence_repository.hpp"
#include "db/scan_repository.hpp"
#include "service/i_scan_starter.hpp"
#include "util/logger.hpp"

#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace presence_limits
{
const int MAX_TRACKERS = 10;
const int MIN_INTERVAL_SECONDS = 10;
const int DEFAULT_INTERVAL_SECONDS = 10;
const int MAX_INTERVAL_SECONDS = 10800;
const int DEFAULT_TIMEOUT_MS = 3000;
const int MAX_TIMEOUT_MS = 30000;
const int DEFAULT_RESULT_LIMIT = 100;
const int MAX_RESULT_LIMIT = 1000;
const int RETENTION_DAYS = 30;
const int RETENTION_MAX_RESULTS = 10000;
}

enum class PresenceStatus
{
    Ok,
    BadRequest,
    NotFound,
    LimitExceeded,
    InternalError
};

struct PresenceWriteResult
{
    PresenceStatus status = PresenceStatus::InternalError;
    std::string message;
    PresenceTracker tracker;
};

struct PresenceRunResult
{
    PresenceStatus status = PresenceStatus::InternalError;
    std::string message;
    PresenceCheckResult result;
};

class IPresenceCheckRunner
{
public:
    virtual ~IPresenceCheckRunner() {}
    virtual PresenceCheckResult run(const PresenceTracker& tracker) = 0;
};

class PresenceService
{
public:
    PresenceService(Database& db, Logger& logger);
    PresenceService(Database& db, Logger& logger, IScanStarter* scan_starter);
    PresenceService(Database& db, Logger& logger, IPresenceCheckRunner& runner);
    PresenceService(Database& db, Logger& logger, IPresenceCheckRunner& runner,
                    IScanStarter* scan_starter);

    std::vector<PresenceTracker> list_trackers();
    std::vector<PresenceTracker> list_enabled_trackers();
    std::unique_ptr<PresenceTracker> get_tracker(int id);
    PresenceWriteResult create_tracker(const PresenceTracker& tracker);
    PresenceWriteResult update_tracker(int id, const PresenceTracker& tracker);
    bool delete_tracker(int id);

    PresenceRunResult run_check(int tracker_id);
    PresenceRunResult run_check(const PresenceTracker& tracker);
    std::vector<PresenceCheckResult> list_results(int tracker_id, int limit);
    int prune_results();

private:
    Database& db_;
    Logger& logger_;
    PresenceRepository repo_;
    ScanRepository scan_repo_;
    std::unique_ptr<IPresenceCheckRunner> owned_runner_;
    IPresenceCheckRunner& runner_;
    IScanStarter* scan_starter_ = nullptr;
    std::mutex auto_scan_mutex_;
    std::set<std::string> auto_scan_attempted_targets_;

    PresenceStatus validate_tracker(const PresenceTracker& tracker, std::string& error);
    PresenceCheckResult persist_result(const PresenceCheckResult& result);
    void maybe_start_initial_scan(const PresenceTracker& tracker,
                                  const PresenceCheckResult& result);
};

#endif
