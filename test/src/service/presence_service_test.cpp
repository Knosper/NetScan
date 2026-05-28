#include "db/database.hpp"
#include "db/schema.hpp"
#include "service/presence_scheduler_service.hpp"
#include "service/presence_service.hpp"
#include "test_output.hpp"
#include "util/logger.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
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
    std::string path = "/tmp/netscan-presence-service-test-XXXXXX.db";
    std::vector<char> buffer(path.begin(), path.end());
    buffer.push_back('\0');
    const int fd = mkstemps(buffer.data(), 3);
    if (fd < 0)
        return std::string();

    close(fd);
    return std::string(buffer.data());
#endif
}

class FakePresenceRunner : public IPresenceCheckRunner
{
public:
    PresenceCheckResult run(const PresenceTracker& tracker) override
    {
        ++calls;
        const int current = ++active;
        int observed = max_active.load();
        while (current > observed && !max_active.compare_exchange_weak(observed, current))
        {
        }

        while (block.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

        --active;
        PresenceCheckResult result;
        result.tracker_id = tracker.id;
        result.status = status;
        result.latency_ms = 1;
        return result;
    }

    std::string status = "up";
    std::atomic<int> calls{0};
    std::atomic<int> active{0};
    std::atomic<int> max_active{0};
    std::atomic<bool> block{false};
};

class FakeScanStarter : public IScanStarter
{
public:
    StartAsyncResult start_async(const ScanRequest& request) override
    {
        requests.push_back(request);
        return {StartAsyncStatus::Accepted, static_cast<int>(requests.size()), ""};
    }

    std::vector<ScanRequest> requests;
};

PresenceTracker make_tracker(int index)
{
    PresenceTracker tracker;
    tracker.target = "127.0.0.1";
    tracker.check_type = "tcp";
    tracker.port = 8000 + index;
    tracker.interval_seconds = presence_limits::MIN_INTERVAL_SECONDS;
    tracker.timeout_ms = presence_limits::DEFAULT_TIMEOUT_MS;
    tracker.enabled = true;
    return tracker;
}

PresenceTracker make_ping_tracker()
{
    PresenceTracker tracker;
    tracker.target = "127.0.0.1";
    tracker.check_type = "ping";
    tracker.interval_seconds = presence_limits::MIN_INTERVAL_SECONDS;
    tracker.timeout_ms = presence_limits::DEFAULT_TIMEOUT_MS;
    tracker.enabled = true;
    return tracker;
}

bool wait_for_calls(FakePresenceRunner& runner, int expected)
{
    for (int i = 0; i < 200; ++i)
    {
        if (runner.calls.load() >= expected)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}
} // namespace

int main()
{
    bool all_ok = true;
    const std::string db_path = make_temp_db_path();
    all_ok = expect(!db_path.empty(), "temporary database path should be created") && all_ok;

    if (!db_path.empty())
    {
        Logger logger;
        Database db(db_path);
        configure_database_runtime(db, logger);
        init_schema(db, logger);

        FakePresenceRunner runner;
        PresenceService service(db, logger, runner);

        PresenceTracker invalid = make_tracker(1);
        invalid.interval_seconds = presence_limits::MIN_INTERVAL_SECONDS - 1;
        PresenceWriteResult invalid_result = service.create_tracker(invalid);
        all_ok = expect(invalid_result.status == PresenceStatus::BadRequest,
                        "service should reject intervals below the minimum") && all_ok;

        PresenceTracker max_interval = make_tracker(1);
        max_interval.interval_seconds = presence_limits::MAX_INTERVAL_SECONDS;
        PresenceWriteResult max_interval_result = service.create_tracker(max_interval);
        all_ok = expect(max_interval_result.status == PresenceStatus::Ok &&
                            max_interval_result.tracker.id > 0,
                        "service should accept intervals at the maximum") && all_ok;
        if (max_interval_result.status == PresenceStatus::Ok)
            service.delete_tracker(max_interval_result.tracker.id);

        PresenceTracker above_max = make_tracker(2);
        above_max.interval_seconds = presence_limits::MAX_INTERVAL_SECONDS + 1;
        PresenceWriteResult above_max_result = service.create_tracker(above_max);
        all_ok = expect(above_max_result.status == PresenceStatus::BadRequest,
                        "service should reject intervals above the maximum") && all_ok;

        PresenceWriteResult created = service.create_tracker(make_tracker(1));
        all_ok = expect(created.status == PresenceStatus::Ok && created.tracker.id > 0,
                        "service should create a valid tracker") && all_ok;

        PresenceRunResult run_result = service.run_check(created.tracker.id);
        all_ok = expect(run_result.status == PresenceStatus::Ok && run_result.result.id > 0,
                        "manual check should persist a result") && all_ok;
        all_ok = expect(service.list_results(created.tracker.id, 10).size() == 1,
                        "manual check result should be listable") && all_ok;

        FakePresenceRunner auto_runner;
        FakeScanStarter scan_starter;
        PresenceService auto_scan_service(db, logger, auto_runner, &scan_starter);
        PresenceWriteResult ping_created = auto_scan_service.create_tracker(make_ping_tracker());
        all_ok = expect(ping_created.status == PresenceStatus::Ok && ping_created.tracker.id > 0,
                        "service should create a ping tracker") && all_ok;
        if (ping_created.status == PresenceStatus::Ok)
        {
            PresenceRunResult first_ping = auto_scan_service.run_check(ping_created.tracker.id);
            all_ok = expect(first_ping.status == PresenceStatus::Ok,
                            "successful ping check should complete") && all_ok;
            all_ok = expect(scan_starter.requests.size() == 1,
                            "first successful ping should queue one initial scan") && all_ok;
            if (!scan_starter.requests.empty())
            {
                all_ok = expect(scan_starter.requests[0].target == "127.0.0.1",
                                "initial scan should target the ping host") && all_ok;
                all_ok = expect(!scan_starter.requests[0].host_discovery_only,
                                "initial scan should not be host-discovery-only") && all_ok;
                all_ok = expect(!scan_starter.requests[0].ports.empty(),
                                "initial scan should use explicit minimal ports") && all_ok;
            }

            PresenceRunResult second_ping = auto_scan_service.run_check(ping_created.tracker.id);
            all_ok = expect(second_ping.status == PresenceStatus::Ok,
                            "second ping check should complete") && all_ok;
            all_ok = expect(scan_starter.requests.size() == 1,
                            "initial scan should not repeat for the same target") && all_ok;
            auto_scan_service.delete_tracker(ping_created.tracker.id);
        }

        for (int i = 2; i <= 5; ++i)
            service.create_tracker(make_tracker(i));

        PresenceTracker disabled = make_tracker(6);
        disabled.enabled = false;
        service.create_tracker(disabled);

        runner.calls.store(0);
        runner.active.store(0);
        runner.max_active.store(0);
        runner.block.store(true);
        PresenceSchedulerService scheduler(service, logger);
        scheduler.start();
        all_ok = expect(wait_for_calls(runner, 4),
                        "presence scheduler should dispatch due enabled trackers") && all_ok;
        all_ok = expect(runner.max_active.load() <= 4,
                        "presence scheduler should cap concurrent checks at four") && all_ok;
        runner.block.store(false);
        all_ok = expect(wait_for_calls(runner, 5),
                        "presence scheduler should dispatch queued due work after capacity frees") &&
                 all_ok;
        all_ok = expect(runner.calls.load() == 5,
                        "presence scheduler should not dispatch disabled trackers") && all_ok;
        scheduler.stop();
    }

    if (!db_path.empty())
        std::remove(db_path.c_str());

    return finish_test("presence_service_test", all_ok);
}
