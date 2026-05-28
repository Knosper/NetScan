#include "db/database.hpp"
#include "db/schedule_repository.hpp"
#include "db/schema.hpp"
#include "service/i_scan_starter.hpp"
#include "service/scheduler_service.hpp"
#include "test_output.hpp"
#include "util/logger.hpp"
#include "util/time_utils.hpp"

#include <atomic>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace
{
bool expect(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << "\n";
        return false;
    }
    return true;
}

std::string make_temp_db_path()
{
#ifdef _WIN32
    char temp_dir[MAX_PATH + 1] = {0};
    const DWORD temp_dir_len = GetTempPathA(MAX_PATH, temp_dir);
    if (temp_dir_len == 0 || temp_dir_len > MAX_PATH)
        return std::string();

    char temp_file[MAX_PATH + 1] = {0};
    if (GetTempFileNameA(temp_dir, "netscan", 0, temp_file) == 0)
        return std::string();

    DeleteFileA(temp_file);
    return std::string(temp_file) + ".db";
#else
    std::string path = "/tmp/netscan-scheduler-service-test-XXXXXX.db";
    std::vector<char> buffer(path.begin(), path.end());
    buffer.push_back('\0');
    const int fd = mkstemps(buffer.data(), 3);
    if (fd < 0)
        return std::string();

    close(fd);
    return std::string(buffer.data());
#endif
}

static const char* SCHEDULER_TIME_FORMAT = "%Y-%m-%dT%H:%M:%SZ";

std::string format_ts(std::time_t t)
{
    return util::format_utc_time(t, SCHEDULER_TIME_FORMAT);
}

class FakeScanStarter : public IScanStarter
{
public:
    StartAsyncResult start_async(const ScanRequest&) override
    {
        ++start_calls;
        return {next_status, -1, ""};
    }

    std::atomic<int> start_calls{0};
    StartAsyncStatus next_status = StartAsyncStatus::Conflict;
};

class ScopedSchedulerFixture
{
public:
    ScopedSchedulerFixture()
        : db_path_(make_temp_db_path()), db_(db_path_)
    {
        configure_database_runtime(db_, logger_);
        init_schema(db_, logger_);
    }

    ~ScopedSchedulerFixture()
    {
        std::remove(db_path_.c_str());
    }

    std::string db_path_;
    Database        db_;
    Logger          logger_;
    FakeScanStarter scan_starter_;
};

void wait_for_release(const std::atomic<bool>& go)
{
    while (!go.load())
        std::this_thread::yield();
}

int insert_due_job(Database& db, const std::string& next_run_at)
{
    ScheduleRepository repo(db);
    ScheduledJob job;
    job.target           = "192.168.1.1";
    job.ports            = "1-1024";
    job.interval_seconds = 3600;
    job.enabled          = true;
    job.next_run_at      = next_run_at;
    return db.write([&repo, &job](sqlite3* h) { return repo.create_job(h, job); });
}

bool test_due_job_is_dispatched()
{
    ScopedSchedulerFixture fixture;
    fixture.scan_starter_.next_status = IScanStarter::StartAsyncStatus::Accepted;

    const std::string past = format_ts(std::time(nullptr) - 60);
    const int job_id = insert_due_job(fixture.db_, past);
    if (!expect(job_id > 0, "job insert should succeed"))
        return false;

    SchedulerService service(fixture.db_, fixture.scan_starter_, fixture.logger_);
    service.tick_once_for_test();

    if (!expect(fixture.scan_starter_.start_calls.load() == 1,
                "due job: start_async must be called exactly once"))
        return false;

    std::unique_ptr<ScheduledJob> updated =
        fixture.db_.read([&fixture, job_id](sqlite3* h) {
            ScheduleRepository repo(fixture.db_);
            return repo.get_job(h, job_id);
        });

    return expect(updated && updated->next_run_at != past,
                  "due job: next_run_at must be updated after dispatch");
}

bool test_future_job_is_not_dispatched()
{
    ScopedSchedulerFixture fixture;
    fixture.scan_starter_.next_status = IScanStarter::StartAsyncStatus::Accepted;

    const std::string future = format_ts(std::time(nullptr) + 3600);
    insert_due_job(fixture.db_, future);

    SchedulerService service(fixture.db_, fixture.scan_starter_, fixture.logger_);
    service.tick_once_for_test();

    return expect(fixture.scan_starter_.start_calls.load() == 0,
                  "future job: start_async must not be called before next_run_at");
}

bool test_second_tick_does_not_redispatch_after_successful_dispatch()
{
    ScopedSchedulerFixture fixture;
    fixture.scan_starter_.next_status = IScanStarter::StartAsyncStatus::Accepted;

    const std::string past = format_ts(std::time(nullptr) - 60);
    insert_due_job(fixture.db_, past);

    SchedulerService service(fixture.db_, fixture.scan_starter_, fixture.logger_);
    service.tick_once_for_test();

    if (!expect(fixture.scan_starter_.start_calls.load() == 1,
                "redispatch: first tick must dispatch the due job"))
        return false;

    service.tick_once_for_test();

    return expect(fixture.scan_starter_.start_calls.load() == 1,
                  "redispatch: second tick must not re-dispatch after next_run_at was moved to future");
}
} // namespace

int main()
{
    bool all_ok = true;

    {
        ScopedSchedulerFixture fixture;
        SchedulerService service(fixture.db_, fixture.scan_starter_, fixture.logger_);

        service.start();
        service.start();
        service.stop();

        all_ok = expect(true, "double start should not terminate and stop should join cleanly") &&
                 all_ok;
    }

    {
        ScopedSchedulerFixture fixture;
        SchedulerService service(fixture.db_, fixture.scan_starter_, fixture.logger_);

        service.start();
        service.stop();
        service.start();
        service.stop();

        all_ok = expect(true, "restart after stop should start and stop cleanly") && all_ok;
    }

    {
        ScopedSchedulerFixture fixture;
        SchedulerService service(fixture.db_, fixture.scan_starter_, fixture.logger_);
        std::atomic<bool> go(false);
        std::vector<std::thread> threads;

        for (int i = 0; i < 8; ++i)
        {
            threads.push_back(std::thread([&service, &go]()
            {
                wait_for_release(go);
                service.start();
            }));
        }

        go.store(true);

        for (std::vector<std::thread>::iterator it = threads.begin(); it != threads.end(); ++it)
            it->join();

        service.stop();
        all_ok = expect(true, "parallel start calls should not race into duplicate workers") &&
                 all_ok;
    }

    {
        ScopedSchedulerFixture fixture;
        SchedulerService service(fixture.db_, fixture.scan_starter_, fixture.logger_);
        service.start();

        std::atomic<bool> go(false);
        std::vector<std::thread> threads;

        for (int i = 0; i < 8; ++i)
        {
            threads.push_back(std::thread([&service, &go]()
            {
                wait_for_release(go);
                service.stop();
            }));
        }

        go.store(true);

        for (std::vector<std::thread>::iterator it = threads.begin(); it != threads.end(); ++it)
            it->join();

        service.stop();
        all_ok = expect(true, "parallel stop calls should join at most one worker thread") &&
                 all_ok;
    }

    all_ok = test_due_job_is_dispatched() && all_ok;
    all_ok = test_future_job_is_not_dispatched() && all_ok;
    all_ok = test_second_tick_does_not_redispatch_after_successful_dispatch() && all_ok;

    return finish_test("scheduler_service_test", all_ok);
}
