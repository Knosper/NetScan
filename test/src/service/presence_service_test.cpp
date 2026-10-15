#include "db/database.hpp"
#include "db/schema.hpp"
#include "service/presence_scheduler_service.hpp"
#include "service/presence_service.hpp"
#include "test_output.hpp"
#include "util/logger.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stdexcept>
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

template <typename Runner>
bool wait_for_calls(Runner& runner, int expected)
{
    for (int i = 0; i < 200; ++i)
    {
        if (runner.calls.load() >= expected)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

class ScopedPresenceDatabase
{
public:
    ScopedPresenceDatabase() : path(make_temp_db_path()), db(new Database(path))
    {
        configure_database_runtime(*db, logger);
        init_schema(*db, logger);
    }
    ~ScopedPresenceDatabase()
    {
        db.reset();
        std::remove(path.c_str());
    }
    Logger logger;
    std::string path;
    std::unique_ptr<Database> db;
};

class ReaderGate
{
public:
    explicit ReaderGate(Database& db) : worker_([this, &db]() {
        db.read([this](sqlite3*) {
            std::unique_lock<std::mutex> lock(mutex_);
            ready_ = true;
            cv_.notify_all();
            cv_.wait(lock, [this]() { return released_; });
        });
    })
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this]() { return ready_; });
    }
    ~ReaderGate() { open(); worker_.join(); }
    void open()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        released_ = true;
        cv_.notify_all();
    }
private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool ready_ = false;
    bool released_ = false;
    std::thread worker_;
};

class MarkedPresenceRunner : public IPresenceCheckRunner
{
public:
    PresenceCheckResult run(const PresenceTracker& tracker) override
    {
        PresenceCheckResult result;
        result.latency_ms = tracker.port;
        result.status = tracker.port == 111 ? "up" : "down";
        result.error = tracker.port == 111 ? "" : "marked failure";
        return result;
    }
};

bool wait_for_result_count(Database& db, int expected)
{
    for (int i = 0; i < 200; ++i)
    {
        const int count = db.write([](sqlite3* h) {
            Stmt stmt;
            if (sqlite3_prepare_v2(h, "SELECT count(*) FROM presence_results;", -1,
                                   &stmt.ptr, nullptr) != SQLITE_OK)
                return -1;
            return sqlite3_step(stmt.ptr) == SQLITE_ROW ? sqlite3_column_int(stmt.ptr, 0) : -1;
        });
        if (count >= expected)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

bool test_concurrent_result_identity()
{
    ScopedPresenceDatabase fixture;
    MarkedPresenceRunner runner;
    PresenceService service(*fixture.db, fixture.logger, runner);
    PresenceTracker first = service.create_tracker(make_tracker(1)).tracker;
    PresenceTracker second = first;
    first.port = 111;
    second.port = 222;
    ReaderGate gate(*fixture.db);
    PresenceRunResult a, b;
    std::thread first_worker([&]() { a = service.run_check(first); });
    bool ok = wait_for_result_count(*fixture.db, 1);
    std::thread second_worker([&]() { b = service.run_check(second); });
    ok = wait_for_result_count(*fixture.db, 2) && ok;
    gate.open();
    first_worker.join();
    second_worker.join();
    ok = expect(ok && a.status == PresenceStatus::Ok && b.status == PresenceStatus::Ok,
                "both concurrent checks should finish and persist their results") && ok;
    ok = expect(a.result.latency_ms == 111 && a.result.status == "up" && a.result.error.empty(),
                "first check must return its own result, not the newer check") && ok;
    ok = expect(b.result.latency_ms == 222 && b.result.status == "down" &&
                b.result.error == "marked failure" && a.result.id != b.result.id,
                "second check must keep its own identity and payload") && ok;
    return expect(!a.result.checked_at.empty() && !b.result.checked_at.empty(),
                  "returned results should include database-generated timestamps") && ok;
}

class ThrowingPresenceRunner : public IPresenceCheckRunner
{
public:
    PresenceCheckResult run(const PresenceTracker&) override
    {
        const int call = ++calls;
        if (call <= 4)
        {
            if (call % 2)
                throw std::runtime_error("injected presence failure");
            throw 42;
        }
        PresenceCheckResult result;
        result.status = "up";
        result.latency_ms = 1;
        return result;
    }
    std::atomic<int> calls{0};
};

bool wait_for_tracker_results(PresenceService& service, const std::vector<int>& ids)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(14);
    while (std::chrono::steady_clock::now() < deadline)
    {
        bool complete = true;
        for (int id : ids)
            complete = !service.list_results(id, 1).empty() && complete;
        if (complete)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

bool test_scheduler_recovers_after_exceptions()
{
    ScopedPresenceDatabase fixture;
    ThrowingPresenceRunner runner;
    PresenceService service(*fixture.db, fixture.logger, runner);
    std::vector<int> ids;
    for (int i = 0; i < 5; ++i)
    {
        const auto created = service.create_tracker(make_tracker(i));
        if (created.status != PresenceStatus::Ok)
            return expect(false, "exception regression fixture should create trackers");
        ids.push_back(created.tracker.id);
    }
    PresenceSchedulerService scheduler(service, fixture.logger);
    scheduler.start();
    bool ok = expect(wait_for_calls(runner, 5),
                     "failed workers should free capacity for the fifth tracker");
    ok = expect(wait_for_tracker_results(service, ids),
                "failed trackers should run again at the regular interval") && ok;
    scheduler.stop();
    return expect(runner.calls.load() >= 9,
                  "all four failed trackers should have been retried") && ok;
}

class ExternalWriteLock
{
public:
    explicit ExternalWriteLock(const std::string& path)
    {
        if (sqlite3_open(path.c_str(), &handle_) != SQLITE_OK ||
            sqlite3_exec(handle_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr) != SQLITE_OK)
        {
            sqlite3_close(handle_);
            throw std::runtime_error("failed to acquire test write lock");
        }
    }
    ~ExternalWriteLock()
    {
        sqlite3_exec(handle_, "ROLLBACK;", nullptr, nullptr, nullptr);
        sqlite3_close(handle_);
    }
private:
    sqlite3* handle_ = nullptr;
};

bool test_scheduler_survives_write_lock()
{
    ScopedPresenceDatabase fixture;
    FakePresenceRunner runner;
    PresenceService service(*fixture.db, fixture.logger, runner);
    const auto created = service.create_tracker(make_tracker(1));
    if (created.status != PresenceStatus::Ok)
        return expect(false, "write-lock fixture should create a tracker");
    bool ok = true;
    {
        ExternalWriteLock blocker(fixture.path);
        PresenceSchedulerService scheduler(service, fixture.logger);
        scheduler.start();
        ok = expect(wait_for_calls(runner, 1), "scheduler should attempt the blocked check");
        scheduler.stop();
        ok = expect(service.list_results(created.tracker.id, 1).empty(),
                    "blocked write must not return a successful persisted result") && ok;
    }
    const auto recovered = service.run_check(created.tracker.id);
    return expect(recovered.status == PresenceStatus::Ok && recovered.result.id > 0,
                  "checks should succeed again after the external write lock is released") && ok;
}
} // namespace

int main()
{
    bool all_ok = test_concurrent_result_identity();
    all_ok = test_scheduler_recovers_after_exceptions() && all_ok;
    all_ok = test_scheduler_survives_write_lock() && all_ok;
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
