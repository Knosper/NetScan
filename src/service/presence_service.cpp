#include "service/presence_service.hpp"
#include "db/write_transaction.hpp"
#include "service/presence_check_runner.hpp"
#include "scan/target_validation.hpp"
#include "util/time_utils.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
const char* PRESENCE_RETENTION_TIME_FORMAT = "%Y-%m-%d %H:%M:%S";

const int STANDARD_PORTS[] = {22, 53, 80, 139, 443, 445, 3389};
const int COMMON_TOP_PORTS[] = {
    80, 23, 443, 21, 22, 25, 3389, 110, 445, 139,
    143, 53, 135, 3306, 8080, 1723, 111, 995, 993, 5900,
    1025, 587, 8888, 199, 1720, 465, 548, 113, 81, 6001,
    10000, 514, 5060, 179, 1026, 2000, 8443, 8000, 32768, 554,
    26, 1433, 49152, 2001, 515, 8008, 49154, 1027, 5666, 646
};

std::string retention_cutoff()
{
    std::time_t t = std::time(nullptr) -
                    static_cast<std::time_t>(presence_limits::RETENTION_DAYS) * 24 * 60 * 60;
    return util::format_utc_time(t, PRESENCE_RETENTION_TIME_FORMAT);
}

PresenceStatus limit_exceeded_result(std::string& error)
{
    error = "presence tracker limit exceeded";
    return PresenceStatus::LimitExceeded;
}

PresenceStatus bad_request_result(const std::string& message, std::string& error)
{
    error = message;
    return PresenceStatus::BadRequest;
}

PresenceStatus validate_target_field(const PresenceTracker& tracker, std::string& error)
{
    if (tracker.target.empty() || !is_valid_scan_target(tracker.target))
        return bad_request_result("field 'target' must be a valid host target", error);
    return PresenceStatus::Ok;
}

PresenceStatus validate_check_type_field(const PresenceTracker& tracker, std::string& error)
{
    if (tracker.check_type == "ping" || tracker.check_type == "tcp" ||
        tracker.check_type == "http")
        return PresenceStatus::Ok;
    return bad_request_result("field 'checkType' must be one of: ping, tcp, http", error);
}

PresenceStatus validate_interval_field(const PresenceTracker& tracker, std::string& error)
{
    if (tracker.interval_seconds < presence_limits::MIN_INTERVAL_SECONDS)
        return bad_request_result(
            "field 'intervalSeconds' must be at least " +
                std::to_string(presence_limits::MIN_INTERVAL_SECONDS),
            error);
    if (tracker.interval_seconds > presence_limits::MAX_INTERVAL_SECONDS)
        return bad_request_result(
            "field 'intervalSeconds' must be at most " +
                std::to_string(presence_limits::MAX_INTERVAL_SECONDS),
            error);
    return PresenceStatus::Ok;
}

PresenceStatus validate_timeout_field(const PresenceTracker& tracker, std::string& error)
{
    if (tracker.timeout_ms <= 0 || tracker.timeout_ms > presence_limits::MAX_TIMEOUT_MS)
        return bad_request_result("field 'timeoutMs' must be between 1 and 30000", error);
    return PresenceStatus::Ok;
}

PresenceStatus validate_port_field(const PresenceTracker& tracker, std::string& error)
{
    if ((tracker.check_type == "tcp" || tracker.check_type == "http") &&
        (tracker.port < 0 || tracker.port > 65535))
        return bad_request_result("field 'port' must be between 1 and 65535", error);

    if (tracker.check_type == "tcp" && tracker.port == 0)
        return bad_request_result("field 'port' is required for tcp checks", error);
    return PresenceStatus::Ok;
}

PresenceStatus validate_http_target(const PresenceTracker& tracker, std::string& error)
{
    HttpTarget parsed;
    if (tracker.check_type == "http" && !tracker.url.empty() &&
        !parse_http_url(tracker, parsed, error))
        return PresenceStatus::BadRequest;
    return PresenceStatus::Ok;
}

const std::string& initial_scan_ports()
{
    static const std::string ports = []() {
        std::set<int> port_set;
        port_set.insert(STANDARD_PORTS,
                        STANDARD_PORTS + sizeof(STANDARD_PORTS) / sizeof(STANDARD_PORTS[0]));
        port_set.insert(COMMON_TOP_PORTS,
                        COMMON_TOP_PORTS + sizeof(COMMON_TOP_PORTS) / sizeof(COMMON_TOP_PORTS[0]));

        std::ostringstream out;
        for (auto it = port_set.begin(); it != port_set.end(); ++it)
        {
            if (it != port_set.begin())
                out << ",";
            out << *it;
        }
        return out.str();
    }();
    return ports;
}
} // namespace

PresenceService::PresenceService(Database& db, Logger& logger)
    : PresenceService(db, logger, nullptr)
{
}

PresenceService::PresenceService(Database& db, Logger& logger, IScanStarter* scan_starter)
    : db_(db), logger_(logger), repo_(db), scan_repo_(db),
      owned_runner_(new DefaultPresenceCheckRunner(&logger)), runner_(*owned_runner_),
      scan_starter_(scan_starter)
{
}

PresenceService::PresenceService(Database& db, Logger& logger, IPresenceCheckRunner& runner)
    : PresenceService(db, logger, runner, nullptr)
{
}

PresenceService::PresenceService(Database& db, Logger& logger, IPresenceCheckRunner& runner,
                                 IScanStarter* scan_starter)
    : db_(db), logger_(logger), repo_(db), scan_repo_(db), runner_(runner),
      scan_starter_(scan_starter)
{
}

std::vector<PresenceTracker> PresenceService::list_trackers()
{
    return db_.read([this](sqlite3* h) { return repo_.list_trackers(h); });
}

std::vector<PresenceTracker> PresenceService::list_enabled_trackers()
{
    return db_.read([this](sqlite3* h) { return repo_.list_enabled_trackers(h); });
}

std::unique_ptr<PresenceTracker> PresenceService::get_tracker(int id)
{
    return db_.read([this, id](sqlite3* h) { return repo_.get_tracker(h, id); });
}

PresenceWriteResult PresenceService::create_tracker(const PresenceTracker& tracker)
{
    std::string error;
    const PresenceStatus field_status = validate_tracker(tracker, error);
    if (field_status != PresenceStatus::Ok)
        return PresenceWriteResult{field_status, error, PresenceTracker()};

    int id = -1;
    bool limit_hit = false;
    db_.write([this, &tracker, &id, &limit_hit](sqlite3* h) {
        WriteTransaction tx(h);
        const int count = repo_.count_trackers(h);
        if (count >= presence_limits::MAX_TRACKERS)
        {
            limit_hit = true;
            return;
        }
        id = repo_.create_tracker(h, tracker);
        tx.commit();
    });
    if (limit_hit)
        return PresenceWriteResult{limit_exceeded_result(error), error, PresenceTracker()};
    if (id <= 0)
    {
        logger_.error("presence_service: failed to create tracker for target '" +
                      tracker.target + "'");
        return PresenceWriteResult{PresenceStatus::InternalError, "failed to create tracker",
                                   PresenceTracker()};
    }

    std::unique_ptr<PresenceTracker> created = get_tracker(id);
    if (!created)
    {
        logger_.error("presence_service: created tracker missing after insert: id=" +
                      std::to_string(id));
        return PresenceWriteResult{PresenceStatus::InternalError, "failed to create tracker",
                                   PresenceTracker()};
    }
    return PresenceWriteResult{PresenceStatus::Ok, "", *created};
}

PresenceWriteResult PresenceService::update_tracker(int id, const PresenceTracker& tracker)
{
    std::string error;
    PresenceTracker updated = tracker;
    updated.id = id;
    const PresenceStatus status = validate_tracker(updated, error);
    if (status != PresenceStatus::Ok)
        return PresenceWriteResult{status, error, PresenceTracker()};

    const bool ok = db_.write([this, &updated](sqlite3* h) {
        return repo_.update_tracker(h, updated);
    });
    if (!ok)
        return PresenceWriteResult{PresenceStatus::NotFound, "tracker not found",
                                   PresenceTracker()};

    std::unique_ptr<PresenceTracker> found = get_tracker(id);
    return PresenceWriteResult{PresenceStatus::Ok, "", found ? *found : PresenceTracker()};
}

bool PresenceService::delete_tracker(int id)
{
    return db_.write([this, id](sqlite3* h) { return repo_.delete_tracker(h, id); });
}

PresenceRunResult PresenceService::run_check(int tracker_id)
{
    std::unique_ptr<PresenceTracker> tracker = get_tracker(tracker_id);
    if (!tracker)
    {
        PresenceRunResult result;
        result.status = PresenceStatus::NotFound;
        result.message = "tracker not found";
        return result;
    }
    return run_check(*tracker);
}

PresenceRunResult PresenceService::run_check(const PresenceTracker& tracker)
{
    PresenceCheckResult result = runner_.run(tracker);
    result.tracker_id = tracker.id;
    PresenceCheckResult saved = persist_result(result);
    prune_results();

    if (saved.id <= 0)
        return PresenceRunResult{PresenceStatus::InternalError, "failed to save result", saved};
    maybe_start_initial_scan(tracker, saved);
    return PresenceRunResult{PresenceStatus::Ok, "", saved};
}

std::vector<PresenceCheckResult> PresenceService::list_results(int tracker_id, int limit)
{
    int bounded_limit = limit <= 0 ? presence_limits::DEFAULT_RESULT_LIMIT : limit;
    bounded_limit = std::min(bounded_limit, presence_limits::MAX_RESULT_LIMIT);
    return db_.read([this, tracker_id, bounded_limit](sqlite3* h) {
        return repo_.list_results(h, tracker_id, bounded_limit);
    });
}

int PresenceService::prune_results()
{
    return db_.write([this](sqlite3* h)
    {
        WriteTransaction tx(h);
        const int old_rows = repo_.prune_results_older_than(h, retention_cutoff());
        const int excess_rows =
            repo_.prune_results_exceeding_total(h, presence_limits::RETENTION_MAX_RESULTS);
        tx.commit();
        return old_rows + excess_rows;
    });
}

PresenceStatus PresenceService::validate_tracker(const PresenceTracker& tracker,
                                                 std::string& error)
{
    const PresenceStatus target_status = validate_target_field(tracker, error);
    if (target_status != PresenceStatus::Ok)
        return target_status;

    const PresenceStatus check_type_status = validate_check_type_field(tracker, error);
    if (check_type_status != PresenceStatus::Ok)
        return check_type_status;

    const PresenceStatus interval_status = validate_interval_field(tracker, error);
    if (interval_status != PresenceStatus::Ok)
        return interval_status;

    const PresenceStatus timeout_status = validate_timeout_field(tracker, error);
    if (timeout_status != PresenceStatus::Ok)
        return timeout_status;

    const PresenceStatus port_status = validate_port_field(tracker, error);
    if (port_status != PresenceStatus::Ok)
        return port_status;

    return validate_http_target(tracker, error);
}

void PresenceService::maybe_start_initial_scan(const PresenceTracker& tracker,
                                               const PresenceCheckResult& result)
{
    if (!scan_starter_ || !tracker.enabled || tracker.check_type != "ping" ||
        result.status != "up")
        return;

    const std::string target = canonicalize_target(tracker.target);
    {
        std::lock_guard<std::mutex> lock(auto_scan_mutex_);
        if (auto_scan_attempted_targets_.find(target) != auto_scan_attempted_targets_.end())
            return;
        auto_scan_attempted_targets_.insert(target);
    }

    const bool has_scan = db_.read([this, &target](sqlite3* h) {
        return scan_repo_.scan_exists_for_target(h, target);
    });
    if (has_scan)
        return;

    ScanRequest request;
    request.target = target;
    request.ports = initial_scan_ports();
    request.host_discovery_only = false;

    const IScanStarter::StartAsyncResult start = scan_starter_->start_async(request);
    if (start.status == IScanStarter::StartAsyncStatus::Accepted)
    {
        logger_.info("presence_service: queued initial minimal scan for target '" + target +
                     "' from tracker " + std::to_string(tracker.id));
    }
    else if (start.status != IScanStarter::StartAsyncStatus::Conflict)
    {
        logger_.warn("presence_service: failed to queue initial minimal scan for target '" +
                     target + "': " + start.error_message);
    }
}

PresenceCheckResult PresenceService::persist_result(const PresenceCheckResult& result)
{
    const int id = db_.write([this, &result](sqlite3* h) {
        return repo_.create_result(h, result);
    });
    if (id <= 0)
    {
        logger_.error("presence_service: failed to persist check result for tracker id " +
                      std::to_string(result.tracker_id));
        return PresenceCheckResult();
    }

    std::vector<PresenceCheckResult> results = list_results(result.tracker_id, 1);
    if (results.empty())
    {
        logger_.error("presence_service: persisted check result missing after insert for "
                      "tracker id " + std::to_string(result.tracker_id));
        return PresenceCheckResult();
    }
    return results.front();
}
