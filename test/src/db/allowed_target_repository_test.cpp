#include "db/allowed_target_repository.hpp"
#include "db/database.hpp"
#include "db/schema.hpp"
#include "db/sqlite_helpers.hpp"
#include "test_output.hpp"
#include "util/logger.hpp"

#include <sqlite3.h>

#include <cstdio>
#include <iostream>
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
    std::string path = "/tmp/netscan-allowed-target-repo-test-XXXXXX.db";
    std::vector<char> buffer(path.begin(), path.end());
    buffer.push_back('\0');
    const int fd = mkstemps(buffer.data(), 3);
    if (fd < 0)
        return std::string();

    close(fd);
    return std::string(buffer.data());
#endif
}

int count_rows_with_missing_created_at(sqlite3* h)
{
    const char* sql =
        "SELECT COUNT(*) FROM user_allowed_targets WHERE created_at IS NULL OR created_at <= 0;";
    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (!stmt.ptr)
        return -1;

    if (sqlite3_step(stmt.ptr) != SQLITE_ROW)
        return -1;
    return sqlite3_column_int(stmt.ptr, 0);
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

        {
            Database db(db_path);
            configure_database_runtime(db, logger);
            init_schema(db, logger);
            init_schema(db, logger);

            AllowedTargetRepository repo(db);

            std::vector<std::string> empty =
                db.read([&repo](sqlite3* h) { return repo.list(h); });
            all_ok = expect(empty.empty(), "new repository should start empty") && all_ok;

            all_ok = expect(db.write([&repo](sqlite3* h)
                                     { return repo.add(h, "192.168.1.0/24"); }),
                            "first allowed target should be inserted") &&
                     all_ok;
            all_ok = expect(!db.write([&repo](sqlite3* h)
                                      { return repo.add(h, "192.168.1.0/24"); }),
                            "duplicate allowed target should fail on UNIQUE constraint") &&
                     all_ok;
            all_ok = expect(db.write([&repo](sqlite3* h)
                                     { return repo.add(h, "10.0.0.0/8"); }),
                            "second allowed target should be inserted") &&
                     all_ok;

            std::vector<std::string> listed =
                db.read([&repo](sqlite3* h) { return repo.list(h); });
            all_ok = expect(listed.size() == 2, "list should return both inserted targets") &&
                     all_ok;
            all_ok = expect(listed.size() == 2 && listed[0] == "192.168.1.0/24" &&
                                listed[1] == "10.0.0.0/8",
                            "list should preserve insertion order by id") &&
                     all_ok;

            const int missing_created_at =
                db.read([](sqlite3* h) { return count_rows_with_missing_created_at(h); });
            all_ok = expect(missing_created_at == 0,
                            "inserted allowed targets should persist created_at timestamps") &&
                     all_ok;

            all_ok = expect(db.write([&repo](sqlite3* h)
                                     { return repo.remove(h, "192.168.1.0/24"); }),
                            "remove should delete an existing target") &&
                     all_ok;
            all_ok = expect(!db.write([&repo](sqlite3* h)
                                      { return repo.remove(h, "192.168.1.0/24"); }),
                            "remove should report false for a missing target") &&
                     all_ok;

            listed = db.read([&repo](sqlite3* h) { return repo.list(h); });
            all_ok = expect(listed.size() == 1 && listed[0] == "10.0.0.0/8",
                            "remove should keep remaining targets intact") &&
                     all_ok;

            all_ok = expect(db.write([&repo](sqlite3* h)
                                     {
                                         return repo.replace_all(
                                             h, {"172.16.0.0/12", "::1/128"});
                                     }),
                            "replace_all should replace the full target set") &&
                     all_ok;

            listed = db.read([&repo](sqlite3* h) { return repo.list(h); });
            all_ok = expect(listed.size() == 2 && listed[0] == "172.16.0.0/12" &&
                                listed[1] == "::1/128",
                            "replace_all should persist the provided target set") &&
                     all_ok;

            all_ok = expect(!db.write([&repo](sqlite3* h)
                                      {
                                          return repo.replace_all(
                                              h, {"127.0.0.0/8", "127.0.0.0/8"});
                                      }),
                            "replace_all should fail when input contains duplicates") &&
                     all_ok;

            listed = db.read([&repo](sqlite3* h) { return repo.list(h); });
            all_ok = expect(listed.size() == 2 && listed[0] == "172.16.0.0/12" &&
                                listed[1] == "::1/128",
                            "failed replace_all should leave the previous rows unchanged") &&
                     all_ok;

            all_ok = expect(db.write([&repo](sqlite3* h) { return repo.clear(h); }),
                            "clear should remove all targets") &&
                     all_ok;
            listed = db.read([&repo](sqlite3* h) { return repo.list(h); });
            all_ok = expect(listed.empty(), "clear should leave an empty table") && all_ok;

            all_ok = expect(db.write([&repo](sqlite3* h)
                                     { return repo.replace_all(h, {"192.0.2.0/24"}); }),
                            "final replace_all fixture should persist before reopen") &&
                     all_ok;
        }

        {
            Database reopened_db(db_path);
            configure_database_runtime(reopened_db, logger);
            init_schema(reopened_db, logger);
            AllowedTargetRepository reopened_repo(reopened_db);

            const std::vector<std::string> persisted =
                reopened_db.read([&reopened_repo](sqlite3* h)
                                 { return reopened_repo.list(h); });
            all_ok = expect(persisted.size() == 1 && persisted[0] == "192.0.2.0/24",
                            "allowed targets should persist across database reopen") &&
                     all_ok;
        }
    }

    if (!db_path.empty())
        std::remove(db_path.c_str());

    return finish_test("allowed_target_repository_test", all_ok);
}
