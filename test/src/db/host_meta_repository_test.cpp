#include "db/database.hpp"
#include "db/host_meta_repository.hpp"
#include "db/schema.hpp"
#include "test_output.hpp"
#include "util/logger.hpp"

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
    std::string path = "/tmp/netscan-host-meta-repo-test-XXXXXX.db";
    std::vector<char> buffer(path.begin(), path.end());
    buffer.push_back('\0');
    const int fd = mkstemps(buffer.data(), 3);
    if (fd < 0)
        return std::string();

    close(fd);
    return std::string(buffer.data());
#endif
}
} // namespace

bool test_clear_field_tags(Database& db, HostMetaRepository& repo)
{
    bool ok = true;
    HostMeta meta;
    meta.display_name = "Switch";
    meta.role         = "network";
    meta.tags         = "infra,edge";
    db.write([&repo, &meta](sqlite3* h) { repo.upsert_meta(h, "10.1.0.1", meta); });

    db.write([&repo](sqlite3* h) { repo.clear_field(h, "10.1.0.1", "tags"); });

    HostMeta after = db.read([&repo](sqlite3* h) { return repo.get_meta(h, "10.1.0.1"); });
    ok = expect(after.tags.empty(), "clear_field(tags) should reset tags to empty") && ok;
    ok = expect(after.display_name == "Switch", "clear_field(tags) should not touch display_name") && ok;
    ok = expect(after.role == "network", "clear_field(tags) should not touch role") && ok;
    return ok;
}

bool test_clear_field_display_name(Database& db, HostMetaRepository& repo)
{
    bool ok = true;
    HostMeta meta;
    meta.display_name = "Printer";
    meta.role         = "peripheral";
    meta.tags         = "office";
    db.write([&repo, &meta](sqlite3* h) { repo.upsert_meta(h, "10.1.0.2", meta); });

    db.write([&repo](sqlite3* h) { repo.clear_field(h, "10.1.0.2", "display_name"); });

    HostMeta after = db.read([&repo](sqlite3* h) { return repo.get_meta(h, "10.1.0.2"); });
    ok = expect(after.display_name.empty(), "clear_field(display_name) should reset display_name to empty") && ok;
    ok = expect(after.role == "peripheral", "clear_field(display_name) should not touch role") && ok;
    ok = expect(after.tags == "office", "clear_field(display_name) should not touch tags") && ok;
    return ok;
}

bool test_clear_field_unknown_is_noop(Database& db, HostMetaRepository& repo)
{
    bool ok = true;
    HostMeta meta;
    meta.display_name = "NAS";
    meta.role         = "storage";
    meta.tags         = "backup";
    db.write([&repo, &meta](sqlite3* h) { repo.upsert_meta(h, "10.1.0.3", meta); });

    db.write([&repo](sqlite3* h) { repo.clear_field(h, "10.1.0.3", "unknown_field"); });

    HostMeta after = db.read([&repo](sqlite3* h) { return repo.get_meta(h, "10.1.0.3"); });
    ok = expect(after.display_name == "NAS", "unknown field: display_name must be unchanged") && ok;
    ok = expect(after.role == "storage", "unknown field: role must be unchanged") && ok;
    ok = expect(after.tags == "backup", "unknown field: tags must be unchanged") && ok;
    return ok;
}

bool test_clear_field_sql_injection_is_noop(Database& db, HostMetaRepository& repo)
{
    bool ok = true;
    HostMeta meta;
    meta.display_name = "AP";
    meta.role         = "wireless";
    meta.tags         = "wifi";
    db.write([&repo, &meta](sqlite3* h) { repo.upsert_meta(h, "10.1.0.4", meta); });

    db.write([&repo](sqlite3* h)
             { repo.clear_field(h, "10.1.0.4", "x; DROP TABLE host_meta"); });
    db.write([&repo](sqlite3* h)
             { repo.clear_field(h, "10.1.0.4", "tags = '' OR 1=1"); });

    HostMeta after = db.read([&repo](sqlite3* h) { return repo.get_meta(h, "10.1.0.4"); });
    ok = expect(after.display_name == "AP", "sql injection vector: display_name must be unchanged") && ok;
    ok = expect(after.role == "wireless", "sql injection vector: role must be unchanged") && ok;
    ok = expect(after.tags == "wifi", "sql injection vector: tags must be unchanged") && ok;

    HostMeta probe = db.read([&repo](sqlite3* h) { return repo.get_meta(h, "10.1.0.3"); });
    ok = expect(probe.role == "storage", "host_meta table must still exist after injection attempt") && ok;
    return ok;
}

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
        HostMetaRepository repo(db);

        // get_meta for unknown IP returns empty defaults
        HostMeta unknown = db.read([&repo](sqlite3* h) { return repo.get_meta(h, "1.2.3.4"); });
        all_ok = expect(unknown.display_name.empty(), "display_name should be empty for unknown ip") && all_ok;
        all_ok = expect(unknown.role.empty(), "role should be empty for unknown ip") && all_ok;
        all_ok = expect(unknown.tags.empty(), "tags should be empty for unknown ip") && all_ok;

        // upsert_meta roundtrip
        HostMeta meta;
        meta.display_name = "Router";
        meta.role = "gateway";
        meta.tags = "infra,core";

        db.write([&repo, &meta](sqlite3* h) { repo.upsert_meta(h, "10.0.0.1", meta); });

        HostMeta loaded =
            db.read([&repo](sqlite3* h) { return repo.get_meta(h, "10.0.0.1"); });
        all_ok = expect(loaded.display_name == "Router", "display_name roundtrip") && all_ok;
        all_ok = expect(loaded.role == "gateway", "role roundtrip") && all_ok;
        all_ok = expect(loaded.tags == "infra,core", "tags roundtrip") && all_ok;

        // upsert_meta updates existing row
        HostMeta updated = meta;
        updated.role = "firewall";
        db.write([&repo, &updated](sqlite3* h)
                 { repo.upsert_meta(h, "10.0.0.1", updated); });

        HostMeta reloaded =
            db.read([&repo](sqlite3* h) { return repo.get_meta(h, "10.0.0.1"); });
        all_ok = expect(reloaded.role == "firewall", "upsert should update existing row") && all_ok;

        // clear_field resets a single field
        db.write([&repo](sqlite3* h) { repo.clear_field(h, "10.0.0.1", "role"); });
        HostMeta after_clear =
            db.read([&repo](sqlite3* h) { return repo.get_meta(h, "10.0.0.1"); });
        all_ok = expect(after_clear.role.empty(), "clear_field should reset role to empty") && all_ok;
        all_ok = expect(after_clear.display_name == "Router", "clear_field should not touch display_name") && all_ok;

        all_ok = test_clear_field_tags(db, repo) && all_ok;
        all_ok = test_clear_field_display_name(db, repo) && all_ok;
        all_ok = test_clear_field_unknown_is_noop(db, repo) && all_ok;
        all_ok = test_clear_field_sql_injection_is_noop(db, repo) && all_ok;
    }

    if (!db_path.empty())
        std::remove(db_path.c_str());

    return finish_test("host_meta_repository_test", all_ok);
}
