#include "service/scheduler_service.hpp"
#include "scan/port_spec_validator.hpp"
#include "scan/scan_types.hpp"
#include "scan/target_validation.hpp"
#include "util/time_utils.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>

namespace
{
const char* SCHEDULER_TIME_FORMAT = "%Y-%m-%dT%H:%M:%SZ";

bool is_due(const std::string& next_run_at)
{
    if (next_run_at.empty())
        return true;

    std::time_t t = 0;
    if (!util::parse_utc_time(next_run_at, SCHEDULER_TIME_FORMAT, t))
        return false;
    return std::time(nullptr) >= t;
}

ScanRequest build_scan_request(const ScheduledJob& job)
{
    ScanRequest request;
    request.target = job.target;
    request.ports = job.ports;
    request.host_discovery_only = job.host_discovery_only;
    return request;
}
} // namespace

SchedulerService::SchedulerService(Database& db, IScanStarter& scan_service, Logger& logger)
    : db_(db), scan_service_(scan_service), logger_(logger), repo_(db)
{
}

SchedulerService::~SchedulerService()
{
    stop();
}

void SchedulerService::start()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (worker_.joinable())
        return;

    stop_requested_ = false;
    worker_ = std::thread(&SchedulerService::run, this);
}

void SchedulerService::stop()
{
    std::thread worker;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_requested_ = true;
        if (worker_.joinable())
            worker = std::move(worker_);
    }
    cv_.notify_all();

    if (worker.joinable())
        worker.join();
}

void SchedulerService::run()
{
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stop_requested_)
    {
        lock.unlock();
        tick();
        lock.lock();
        cv_.wait_for(lock, std::chrono::seconds(10),
                     [this] { return stop_requested_; });
    }
}

std::vector<ScheduledJob> SchedulerService::list_jobs()
{
    return db_.read([this](sqlite3* h) { return repo_.list_jobs(h); });
}

std::unique_ptr<ScheduledJob> SchedulerService::get_job(int id)
{
    return db_.read([this, id](sqlite3* h) { return repo_.get_job(h, id); });
}

int SchedulerService::create_job(const ScheduledJob& job)
{
    if (!is_valid_scan_target(job.target))
        return 0;

    if (!validate_port_spec(job.ports).ok)
        return 0;

    return db_.write([this, &job](sqlite3* h) { return repo_.create_job(h, job); });
}

bool SchedulerService::delete_job(int id)
{
    return db_.write([this, id](sqlite3* h) { return repo_.delete_job(h, id); });
}

bool SchedulerService::set_enabled(int id, bool enabled)
{
    return db_.write([this, id, enabled](sqlite3* h) { return repo_.set_enabled(h, id, enabled); });
}

void SchedulerService::tick()
{
    std::vector<ScheduledJob> jobs = db_.read([this](sqlite3* h) { return repo_.list_jobs(h); });
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

    for (const ScheduledJob& job : jobs)
        process_job(job, now);
}

void SchedulerService::process_job(const ScheduledJob& job,
                                   std::chrono::steady_clock::time_point now)
{
    if (!validate_due_job(job, now))
        return;

    const IScanStarter::StartAsyncResult result =
        scan_service_.start_async(build_scan_request(job));
    if (result.status == IScanStarter::StartAsyncStatus::Conflict)
    {
        logger_.info("Scheduler: scan already running, skipping job " +
                     std::to_string(job.id));
        return;
    }

    if (result.status != IScanStarter::StartAsyncStatus::Accepted)
    {
        logger_.warn("Scheduler: failed to start scan for job " + std::to_string(job.id) +
                     ": " + result.error_message);
        return;
    }

    if (!update_job_run_times(job))
    {
        logger_.warn("Scheduler: failed to update run times for job " +
                     std::to_string(job.id) + ", backing off retries");
        set_run_retry_backoff(job.id, job.interval_seconds, now);
        return;
    }

    clear_run_retry_backoff(job.id);
}

bool SchedulerService::validate_due_job(const ScheduledJob& job,
                                        std::chrono::steady_clock::time_point now)
{
    if (!job.enabled || !is_due(job.next_run_at))
        return false;
    if (should_skip_due_to_backoff(job.id, now))
        return false;
    if (is_valid_scan_target(job.target))
        return true;

    logger_.warn("Scheduler: invalid target in job " + std::to_string(job.id) + ", skipping");
    return false;
}

bool SchedulerService::update_job_run_times(const ScheduledJob& job)
{
    const std::time_t now = std::time(nullptr);
    const std::string last = util::format_utc_time(now, SCHEDULER_TIME_FORMAT);
    const std::string next = util::format_utc_time(now + job.interval_seconds,
                                                   SCHEDULER_TIME_FORMAT);
    return db_.write([this, &job, &last, &next](sqlite3* h) {
        return repo_.update_job_run_times(h, job.id, last, next);
    });
}

bool SchedulerService::should_skip_due_to_backoff(
    int job_id, std::chrono::steady_clock::time_point now)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::map<int, std::chrono::steady_clock::time_point>::iterator it =
        run_retry_backoff_.find(job_id);
    if (it == run_retry_backoff_.end())
        return false;
    if (now < it->second)
        return true;

    run_retry_backoff_.erase(it);
    return false;
}

void SchedulerService::set_run_retry_backoff(
    int job_id, int interval_seconds, std::chrono::steady_clock::time_point now)
{
    const int safe_interval_seconds = std::max(1, interval_seconds);
    const std::chrono::steady_clock::time_point next_retry =
        now + std::chrono::seconds(safe_interval_seconds);

    std::lock_guard<std::mutex> lock(mutex_);
    run_retry_backoff_[job_id] = next_retry;
}

void SchedulerService::clear_run_retry_backoff(int job_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    run_retry_backoff_.erase(job_id);
}
