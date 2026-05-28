#include "app/config.hpp"
#include "db/allowed_target_repository.hpp"
#include "db/database.hpp"
#include "db/schema.hpp"
#include "service/settings_service.hpp"
#include "test_output.hpp"
#include "util/logger.hpp"

#include <cstdio>
#include <fstream>
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

bool expect_targets(const std::vector<std::string>& actual,
                    const std::vector<std::string>& expected,
                    const std::string& message)
{
    return expect(actual == expected, message);
}

std::string read_file_or_empty(const std::string& path)
{
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file.is_open())
        return "";

    return std::string((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
}

std::string make_temp_db_path(const std::string& path_template)
{
#ifdef _WIN32
    (void)path_template;

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
    std::vector<char> buffer(path_template.begin(), path_template.end());
    buffer.push_back('\0');
    const int fd = mkstemps(buffer.data(), 3);
    if (fd < 0)
        return std::string();

    close(fd);
    return std::string(buffer.data());
#endif
}
} // namespace

int main()
{
    bool all_ok = true;

    const std::string db_path = make_temp_db_path("/tmp/netscan-settings-service-test-XXXXXX.db");
    const std::string conf_path =
        make_temp_db_path("/tmp/netscan-settings-service-config-test-XXXXXX.db");
    all_ok = expect(!db_path.empty(), "temporary database path should be created") && all_ok;
    all_ok = expect(!conf_path.empty(), "temporary config path should be created") && all_ok;

    if (all_ok)
    {
        Logger logger;
        Database db(db_path);
        configure_database_runtime(db, logger);
        init_schema(db, logger);

        PersistedConfig persisted = make_default_persisted_config();
        persisted.log_level = "warn";
        persisted.scan_cooldown_seconds = 42;
        std::string error;
        all_ok = expect(write_persisted_config_atomically(conf_path, persisted, error),
                        "settings service fixture should write persisted config") &&
                 all_ok;

        SettingsService service(db, conf_path, logger);
        AllowedTargetRepository repo(db);

        const SettingsService::Settings loaded_empty = service.load_settings();
        all_ok = expect(loaded_empty.log_level == "warn",
                        "load_settings should still read log_level from conf.ini") &&
                 all_ok;
        all_ok = expect(loaded_empty.scan_cooldown_seconds == 42,
                        "load_settings should still read scan_cooldown_seconds from conf.ini") &&
                 all_ok;
        all_ok = expect(loaded_empty.user_allowed_targets.empty(),
                        "load_settings should treat an empty DB allowlist as authoritative") &&
                 all_ok;

        SettingsService::Settings updated = loaded_empty;
        updated.log_level = "debug";
        updated.scan_cooldown_seconds = 7;
        updated.user_allowed_targets = {"192.168.50.0/24", "10.20.30.0/24"};
        const SettingsService::SaveResult save_result = service.save_settings(updated);
        all_ok = expect(save_result.status == SettingsService::SaveStatus::Saved,
                        "save_settings should persist valid settings") &&
                 all_ok;

        const std::vector<std::string> stored_targets = db.read(
            [&repo](sqlite3* h)
            {
                return repo.list(h);
            });
        all_ok = expect_targets(stored_targets, updated.user_allowed_targets,
                                "save_settings should persist user_allowed_targets in the DB") &&
                 all_ok;

        const SettingsService::Settings reloaded = service.load_settings();
        all_ok = expect(reloaded.log_level == "debug",
                        "load_settings should return the updated log level") &&
                 all_ok;
        all_ok = expect(reloaded.scan_cooldown_seconds == 7,
                        "load_settings should return the updated cooldown") &&
                 all_ok;
        all_ok = expect_targets(reloaded.user_allowed_targets, updated.user_allowed_targets,
                                "load_settings should return DB-backed allowed targets") &&
                 all_ok;

        const AppSettings file_only = load_raw_settings(conf_path);
        all_ok = expect(file_only.log_level == "debug",
                        "save_settings should still persist log_level in conf.ini") &&
                 all_ok;
        all_ok = expect(file_only.scan_cooldown_seconds == 7,
                        "save_settings should still persist scan_cooldown_seconds in conf.ini") &&
                 all_ok;
        all_ok = expect(read_file_or_empty(conf_path).find("user_allowed_targets=") ==
                            std::string::npos,
                        "save_settings should no longer mirror user_allowed_targets into conf.ini") &&
                 all_ok;

        SettingsService restarted(db, conf_path, logger);
        const SettingsService::Settings after_restart = restarted.load_settings();
        all_ok = expect_targets(after_restart.user_allowed_targets, updated.user_allowed_targets,
                                "recreated SettingsService should read persisted DB allowed targets") &&
                 all_ok;

        SettingsService::Settings ipv6_disallowed = after_restart;
        ipv6_disallowed.user_allowed_targets = {"::1/128"};
        const SettingsService::SaveResult ipv6_result = service.save_settings(ipv6_disallowed);
        all_ok = expect(ipv6_result.status == SettingsService::SaveStatus::BadRequest,
                        "save_settings should reject IPv6 entries in user_allowed_targets") &&
                 all_ok;
        all_ok = expect(ipv6_result.error == "field 'user_allowed_targets[0]' IPv6 entries are not allowed",
                        "IPv6 rejection should include an indexed validation message") &&
                 all_ok;
    }

    if (!db_path.empty())
        std::remove(db_path.c_str());
    if (!conf_path.empty())
        std::remove(conf_path.c_str());
    if (!conf_path.empty())
        std::remove((conf_path + ".tmp").c_str());

    return finish_test("settings_service_test", all_ok);
}
