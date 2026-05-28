#include "db/database.hpp"
#include "db/presence_repository.hpp"
#include "db/schema.hpp"
#include "test_output.hpp"
#include "util/logger.hpp"

#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
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
    std::string path = "/tmp/netscan-presence-repo-test-XXXXXX.db";
    std::vector<char> buffer(path.begin(), path.end());
    buffer.push_back('\0');
    const int fd = mkstemps(buffer.data(), 3);
    if (fd < 0)
        return std::string();

    close(fd);
    return std::string(buffer.data());
#endif
}

PresenceTracker make_tracker()
{
    PresenceTracker tracker;
    tracker.target = "127.0.0.1";
    tracker.check_type = "tcp";
    tracker.port = 80;
    tracker.interval_seconds = 300;
    tracker.timeout_ms = 3000;
    tracker.enabled = true;
    return tracker;
}

PresenceCheckResult make_result(int tracker_id, const std::string& status)
{
    PresenceCheckResult result;
    result.tracker_id = tracker_id;
    result.status = status;
    result.latency_ms = status == "up" ? 12 : -1;
    result.error = status == "up" ? "" : "failed";
    return result;
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
        PresenceRepository repo(db);

        int tracker_id = db.write([&repo](sqlite3* h)
        {
            return repo.create_tracker(h, make_tracker());
        });
        all_ok = expect(tracker_id > 0, "tracker should be created") && all_ok;

        std::unique_ptr<PresenceTracker> loaded = db.read([&repo, tracker_id](sqlite3* h)
        {
            return repo.get_tracker(h, tracker_id);
        });
        all_ok = expect(loaded && loaded->target == "127.0.0.1",
                        "tracker should be readable after create") && all_ok;

        PresenceTracker updated = make_tracker();
        updated.id = tracker_id;
        updated.enabled = false;
        updated.port = 443;
        all_ok = expect(db.write([&repo, &updated](sqlite3* h)
                                 { return repo.update_tracker(h, updated); }),
                        "tracker should be updated") && all_ok;

        std::vector<PresenceTracker> enabled = db.read([&repo](sqlite3* h)
        {
            return repo.list_enabled_trackers(h);
        });
        all_ok = expect(enabled.empty(), "disabled tracker should not be listed as enabled") &&
                 all_ok;

        int first_result = db.write([&repo, tracker_id](sqlite3* h)
        {
            return repo.create_result(h, make_result(tracker_id, "up"));
        });
        int second_result = db.write([&repo, tracker_id](sqlite3* h)
        {
            return repo.create_result(h, make_result(tracker_id, "down"));
        });
        all_ok = expect(first_result > 0 && second_result > 0,
                        "presence results should be persisted") && all_ok;

        std::vector<PresenceCheckResult> results = db.read([&repo, tracker_id](sqlite3* h)
        {
            return repo.list_results(h, tracker_id, 1);
        });
        all_ok = expect(results.size() == 1, "result list should honor limit") && all_ok;
        all_ok = expect(!results.empty() && results.front().status == "down",
                        "result list should return newest result first") && all_ok;

        int third_result = db.write([&repo, tracker_id](sqlite3* h)
        {
            return repo.create_result(h, make_result(tracker_id, "up"));
        });
        all_ok = expect(third_result > 0, "third presence result should be persisted") && all_ok;

        int pruned = db.write([&repo](sqlite3* h)
        {
            return repo.prune_results_exceeding_total(h, 2);
        });
        all_ok = expect(pruned == 1, "prune should remove exactly one oldest result") && all_ok;

        results = db.read([&repo, tracker_id](sqlite3* h)
        {
            return repo.list_results(h, tracker_id, 10);
        });
        all_ok = expect(results.size() == 2, "prune should keep only the configured newest results") &&
                 all_ok;
        all_ok = expect(results.size() == 2 && results[0].id == third_result &&
                            results[1].id == second_result,
                        "prune should keep the newest results by checked_at/id ordering") &&
                 all_ok;

        pruned = db.write([&repo](sqlite3* h)
        {
            return repo.prune_results_exceeding_total(h, 0);
        });
        all_ok = expect(pruned == 2, "prune with keep_count=0 should remove all remaining results") &&
                 all_ok;

        results = db.read([&repo, tracker_id](sqlite3* h)
        {
            return repo.list_results(h, tracker_id, 10);
        });
        all_ok = expect(results.empty(), "prune with keep_count=0 should leave no results") &&
                 all_ok;

        all_ok = expect(db.write([&repo, tracker_id](sqlite3* h)
                                 { return repo.delete_tracker(h, tracker_id); }),
                        "tracker should be deleted") && all_ok;
        results = db.read([&repo, tracker_id](sqlite3* h)
                          { return repo.list_results(h, tracker_id, 10); });
        all_ok = expect(results.empty(), "tracker deletion should remove its results") && all_ok;
    }

    if (!db_path.empty())
        std::remove(db_path.c_str());

    return finish_test("presence_repository_test", all_ok);
}
