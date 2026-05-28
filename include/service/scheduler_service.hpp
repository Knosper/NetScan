#ifndef SERVICE_SCHEDULER_SERVICE_HPP
#define SERVICE_SCHEDULER_SERVICE_HPP

#include "db/database.hpp"
#include "db/schedule_repository.hpp"
#include "service/i_scan_starter.hpp"
#include "util/logger.hpp"
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>

class SchedulerService
{
public:
    SchedulerService(Database& db, IScanStarter& scan_service, Logger& logger);
    ~SchedulerService();

    void start();
    void stop();
    void tick_once_for_test() { tick(); }

    // CRUD operations — safe to call from HTTP threads
    std::vector<ScheduledJob>     list_jobs();
    std::unique_ptr<ScheduledJob> get_job(int id);
    int                           create_job(const ScheduledJob& job);
    bool                          delete_job(int id);
    bool                          set_enabled(int id, bool enabled);

private:
    Database&          db_;
    IScanStarter&      scan_service_;
    Logger&            logger_;
    ScheduleRepository repo_;

    std::mutex              mutex_;
    std::condition_variable cv_;
    bool                    stop_requested_ = false;
    std::thread             worker_;
    std::map<int, std::chrono::steady_clock::time_point> run_retry_backoff_;

    void run();
    void tick();
    void process_job(const ScheduledJob& job, std::chrono::steady_clock::time_point now);
    bool validate_due_job(const ScheduledJob& job, std::chrono::steady_clock::time_point now);
    bool update_job_run_times(const ScheduledJob& job);
    bool should_skip_due_to_backoff(int job_id, std::chrono::steady_clock::time_point now);
    void set_run_retry_backoff(int job_id, int interval_seconds,
                               std::chrono::steady_clock::time_point now);
    void clear_run_retry_backoff(int job_id);
};

#endif
