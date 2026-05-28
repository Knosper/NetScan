#include "service/presence_scheduler_service.hpp"

namespace
{
const int MAX_CONCURRENT_CHECKS = 4;
}

PresenceSchedulerService::PresenceSchedulerService(PresenceService& presence_service,
                                                   Logger& logger)
    : presence_service_(presence_service), logger_(logger)
{
}

PresenceSchedulerService::~PresenceSchedulerService()
{
    stop();
}

void PresenceSchedulerService::start()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (worker_.joinable())
        return;

    stop_requested_ = false;
    worker_ = std::thread(&PresenceSchedulerService::run, this);
}

void PresenceSchedulerService::stop()
{
    std::thread worker;
    std::vector<std::thread> check_workers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_requested_ = true;
        if (worker_.joinable())
            worker = std::move(worker_);
    }
    cv_.notify_all();

    if (worker.joinable())
        worker.join();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (std::map<int, std::thread>::iterator it = check_workers_.begin();
             it != check_workers_.end(); ++it)
        {
            check_workers.push_back(std::move(it->second));
        }
        check_workers_.clear();
        completed_check_workers_.clear();
    }

    for (std::vector<std::thread>::iterator it = check_workers.begin();
         it != check_workers.end(); ++it)
    {
        if (it->joinable())
            it->join();
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        active_checks_ = 0;
        completed_check_workers_.clear();
        running_trackers_.clear();
    }
}

void PresenceSchedulerService::run()
{
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stop_requested_)
    {
        lock.unlock();
        tick();
        lock.lock();
        cv_.wait_for(lock, std::chrono::seconds(1), [this]() { return stop_requested_; });
    }
}

void PresenceSchedulerService::tick()
{
    reap_completed_checks();

    std::vector<PresenceTracker> trackers = presence_service_.list_enabled_trackers();
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

    for (const PresenceTracker& tracker : trackers)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_requested_)
            return;
        if (!should_dispatch_locked(tracker, now))
            continue;
        dispatch_check(tracker);
    }

    reap_completed_checks();
}

bool PresenceSchedulerService::should_dispatch_locked(
    const PresenceTracker& tracker, std::chrono::steady_clock::time_point now)
{
    if (active_checks_ >= MAX_CONCURRENT_CHECKS)
        return false;
    if (running_trackers_.find(tracker.id) != running_trackers_.end())
        return false;

    std::map<int, std::chrono::steady_clock::time_point>::iterator it = next_due_.find(tracker.id);
    if (it != next_due_.end() && now < it->second)
        return false;

    next_due_[tracker.id] = now + std::chrono::seconds(tracker.interval_seconds);
    running_trackers_.insert(tracker.id);
    ++active_checks_;
    return true;
}

void PresenceSchedulerService::dispatch_check(const PresenceTracker& tracker)
{
    const int check_worker_id = next_check_worker_id_++;
    std::thread check_worker([this, tracker, check_worker_id]()
    {
        PresenceRunResult result = presence_service_.run_check(tracker);
        if (result.status != PresenceStatus::Ok)
        {
            logger_.warn("Presence scheduler: check failed for tracker " +
                         std::to_string(tracker.id) + ": " + result.message);
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            running_trackers_.erase(tracker.id);
            completed_check_workers_.push_back(check_worker_id);
            --active_checks_;
        }
        cv_.notify_all();
    });
    check_workers_.insert(std::make_pair(check_worker_id, std::move(check_worker)));
}

void PresenceSchedulerService::reap_completed_checks()
{
    std::vector<std::thread> check_workers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (std::vector<int>::iterator it = completed_check_workers_.begin();
             it != completed_check_workers_.end(); ++it)
        {
            std::map<int, std::thread>::iterator worker_it = check_workers_.find(*it);
            if (worker_it == check_workers_.end())
                continue;
            check_workers.push_back(std::move(worker_it->second));
            check_workers_.erase(worker_it);
        }
        completed_check_workers_.clear();
    }

    for (std::vector<std::thread>::iterator it = check_workers.begin();
         it != check_workers.end(); ++it)
    {
        if (it->joinable())
            it->join();
    }
}
