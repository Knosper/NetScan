#ifndef SERVICE_PRESENCE_SCHEDULER_SERVICE_HPP
#define SERVICE_PRESENCE_SCHEDULER_SERVICE_HPP

#include "service/presence_service.hpp"
#include "util/logger.hpp"

#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

class PresenceSchedulerService
{
public:
    PresenceSchedulerService(PresenceService& presence_service, Logger& logger);
    ~PresenceSchedulerService();

    void start();
    void stop();

private:
    PresenceService& presence_service_;
    Logger& logger_;

    std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_requested_ = false;
    int active_checks_ = 0;
    int next_check_worker_id_ = 1;
    std::thread worker_;
    std::map<int, std::thread> check_workers_;
    std::vector<int> completed_check_workers_;
    std::set<int> running_trackers_;
    std::map<int, std::chrono::steady_clock::time_point> next_due_;

    void run();
    void tick();
    void dispatch_check(const PresenceTracker& tracker);
    void reap_completed_checks();
    bool should_dispatch_locked(const PresenceTracker& tracker,
                                std::chrono::steady_clock::time_point now);
};

#endif
