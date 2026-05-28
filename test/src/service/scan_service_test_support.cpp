#include "scan_service_test_support.hpp"

#include "db/sqlite_helpers.hpp"
#include "db/write_transaction.hpp"
#include "scan/port_spec_validator.hpp"
#include "service/scan_persistence.hpp"
#include "util/logger.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#ifdef _WIN32
#include <windows.h>
#else
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{
#ifdef _WIN32
std::string make_windows_temp_db_path()
{
    char temp_dir[MAX_PATH + 1] = {0};
    const DWORD temp_dir_len = GetTempPathA(MAX_PATH, temp_dir);
    if (temp_dir_len == 0 || temp_dir_len > MAX_PATH)
        return std::string();

    char temp_file[MAX_PATH + 1] = {0};
    if (GetTempFileNameA(temp_dir, "netscan", 0, temp_file) == 0)
        return std::string();

    DeleteFileA(temp_file);
    return std::string(temp_file) + ".db";
}

std::string make_windows_temp_dir_path()
{
    char temp_dir[MAX_PATH + 1] = {0};
    const DWORD temp_dir_len = GetTempPathA(MAX_PATH, temp_dir);
    if (temp_dir_len == 0 || temp_dir_len > MAX_PATH)
        return std::string();

    char temp_file[MAX_PATH + 1] = {0};
    if (GetTempFileNameA(temp_dir, "netscan", 0, temp_file) == 0)
        return std::string();

    DeleteFileA(temp_file);
    if (!CreateDirectoryA(temp_file, nullptr))
        return std::string();

    return std::string(temp_file);
}

std::string build_windows_fake_nmap_script(const std::string& output_line, bool keep_running)
{
    std::string escaped;
    for (std::string::const_iterator it = output_line.begin(); it != output_line.end(); ++it)
    {
        const char c = *it;
        if (c == '%' || c == '^' || c == '&' || c == '|' ||
            c == '<' || c == '>' || c == '!' || c == '"')
        {
            escaped += '^';
        }
        escaped.push_back(c);
    }

    std::string script;
    script += "@echo off\r\n";
    script += "echo " + escaped + "\r\n";
    if (!keep_running)
        return script + "exit /b 0\r\n";
    script += ":loop\r\n";
    script += "timeout /t 1 /nobreak >nul\r\n";
    script += "goto loop\r\n";
    return script;
}

bool set_test_env(const char* name, const std::string& value)
{
    return SetEnvironmentVariableA(name, value.c_str()) != 0;
}

bool unset_test_env(const char* name)
{
    return SetEnvironmentVariableA(name, nullptr) != 0;
}
#else
#if defined(_WIN32) && !defined(__MINGW32__)
#error "setenv/unsetenv are not available in the MSVC CRT. Build with MinGW-w64 or provide a shim."
#endif
std::string make_posix_temp_db_path(const char* path_template)
{
    std::string path = path_template ? path_template : "";
    if (path.empty())
        return std::string();

    std::vector<char> buffer(path.begin(), path.end());
    buffer.push_back('\0');

    const int fd = mkstemps(buffer.data(), 3);
    if (fd < 0)
        return std::string();

    close(fd);
    return std::string(buffer.data());
}

bool set_test_env(const char* name, const std::string& value)
{
    return setenv(name, value.c_str(), 1) == 0;
}

bool unset_test_env(const char* name)
{
    return unsetenv(name) == 0;
}
#endif

bool exec_locked_sql(sqlite3* h, Logger& logger, const char* sql)
{
    char* error = nullptr;
    const int rc = sqlite3_exec(h, sql, nullptr, nullptr, &error);
    if (rc == SQLITE_OK)
        return true;

    const std::string message = error ? error : "unknown sqlite error";
    logger.error(std::string("test fixture failed to execute SQL: ") + message);
    sqlite3_free(error);
    return false;
}

bool persist_completed_scan(sqlite3* h, ScanRepository& repo, Logger& logger, int scan_id,
                            const ServiceTestScanFixture& fixture, const ScanResult& result)
{
    if (!exec_locked_sql(h, logger, "BEGIN IMMEDIATE;"))
        return false;

    bool ok = true;
    CompletedScanSnapshot snapshot;
    snapshot.scan_id = scan_id;
    snapshot.host_discovery_only = fixture.host_discovery_only;
    snapshot.port_coverage = derive_scan_port_coverage(
        {fixture.request.target, fixture.request.ports, fixture.host_discovery_only,
         fixture.request.nmap_executable});
    snapshot.snapshot = fixture.snapshot;
    ok = persist_completed_scan_snapshot(h, repo, logger, snapshot);
    if (ok)
        ok = repo.mark_scan_completed(h, scan_id, result, "2026-01-01 00:00:02");

    if (!ok)
    {
        exec_locked_sql(h, logger, "ROLLBACK;");
        return repo.mark_scan_failed(h, scan_id, {ScanOutcome::Failed,
                                               "failed to persist completed scan results",
                                               "",
                                               -1,
                                               "",
                                               ""},
                                     "2026-01-01 00:00:02");
    }

    if (!exec_locked_sql(h, logger, "COMMIT;"))
    {
        exec_locked_sql(h, logger, "ROLLBACK;");
        return repo.mark_scan_failed(h, scan_id, {ScanOutcome::Failed,
                                               "failed to persist completed scan results",
                                               "",
                                               -1,
                                               "",
                                               ""},
                                     "2026-01-01 00:00:02");
    }

    return true;
}

std::string current_working_directory()
{
#ifdef _WIN32
    char buffer[MAX_PATH + 1] = {0};
    const DWORD length = GetCurrentDirectoryA(MAX_PATH, buffer);
    if (length == 0 || length > MAX_PATH)
        return std::string();
    return std::string(buffer, length);
#else
    char buffer[PATH_MAX + 1] = {0};
    if (getcwd(buffer, sizeof(buffer)) == nullptr)
        return std::string();
    return std::string(buffer);
#endif
}

std::string normalize_test_path(const char* test_file_path)
{
    std::string path = test_file_path ? test_file_path : "";
    if (path.empty())
        return std::string();

    if (!path.empty() && path[0] == '/')
        return path;

#ifdef _WIN32
    if (path.size() > 1 && path[1] == ':')
        return path;
#endif

    const std::string cwd = current_working_directory();
    if (cwd.empty())
        return path;

    return cwd + "/" + path;
}

std::string parent_dir(std::string path)
{
    const std::string::size_type slash = path.find_last_of("/\\");
    if (slash == std::string::npos)
        return std::string();
    return path.substr(0, slash);
}

#ifndef _WIN32
std::string build_fake_nmap_script(const std::string& output_line, bool keep_running)
{
    std::string script;
    script += "#!/bin/sh\n";
    script += "trap '' TERM\n";
    script += "printf '";
    for (std::string::const_iterator it = output_line.begin(); it != output_line.end(); ++it)
    {
        if (*it == '\'')
            script += "'\\''";
        else
            script.push_back(*it);
    }
    script += "\\n'\n";
    if (!keep_running)
        return script + "exit 0\n";
    script += "while :; do sleep 1; done\n";
    return script;
}
#endif
} // namespace

bool expect(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << "\n";
        return false;
    }
    return true;
}

bool expect_response_status(const httplib::Result& result, int status, const std::string& message)
{
    bool ok = true;
    ok = expect(static_cast<bool>(result), message + " should return an HTTP response") && ok;
    if (result)
        ok = expect(result->status == status,
                    message + " should return HTTP " + std::to_string(status)) &&
             ok;
    return ok;
}

std::string make_temp_db_path(const char* path_template)
{
#ifdef _WIN32
    (void)path_template;
    return make_windows_temp_db_path();
#else
    return make_posix_temp_db_path(path_template);
#endif
}

TestRepoPaths make_test_repo_paths(const char* test_file_path)
{
    const std::string path = normalize_test_path(test_file_path);

    TestRepoPaths paths;
    paths.repo_root = parent_dir(parent_dir(parent_dir(parent_dir(path))));
    paths.web_dir = paths.repo_root + "/resources/web";
    paths.conf_path = paths.repo_root + "/conf.ini";
    return paths;
}

void configure_test_client(httplib::Client& client, int read_timeout_seconds)
{
    client.set_connection_timeout(2, 0);
    client.set_read_timeout(read_timeout_seconds, 0);
    client.set_write_timeout(read_timeout_seconds, 0);
}

int count_ports_for_host(Database& db, int host_id)
{
    return db.read([host_id](sqlite3* h) -> int {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(h, "SELECT COUNT(*) FROM ports WHERE host_id=?;", -1, &stmt,
                               nullptr) != SQLITE_OK)
        {
            return -1;
        }

        sqlite3_bind_int(stmt, 1, host_id);
        int count = -1;
        if (sqlite3_step(stmt) == SQLITE_ROW)
            count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        return count;
    });
}

int persist_scan(Database& db, ScanRepository& repo, const ServiceTestScanFixture& fixture)
{
    return db.write([&](sqlite3* h) -> int {
    const int scan_id = repo.insert_scan_queued(h, fixture.request);
    if (scan_id < 0)
        return -1;

    if (fixture.state == PersistedScanState::Queued)
        return scan_id;

    if (!repo.mark_scan_running(h, scan_id, "2026-01-01 00:00:01"))
        return -1;

    if (fixture.state == PersistedScanState::Running)
        return scan_id;

    ScanResult result;
    result.outcome = ScanOutcome::Failed;
    result.message = "scan failed";
    if (fixture.state == PersistedScanState::Completed)
    {
        result.outcome = ScanOutcome::Completed;
        result.message = "scan completed";
    }
    else if (fixture.state == PersistedScanState::Aborted)
    {
        result.outcome = ScanOutcome::Aborted;
        result.message = "scan aborted";
    }
    else if (fixture.state == PersistedScanState::DependencyMissing)
    {
        result.outcome = ScanOutcome::DependencyMissing;
        result.message = "nmap missing";
    }
    result.command = "nmap -- test";
    result.exit_code = (fixture.state == PersistedScanState::Completed) ? 0 : -1;

    bool ok = false;
    if (fixture.state == PersistedScanState::Completed)
    {
        Logger logger;
        ok = persist_completed_scan(h, repo, logger, scan_id, fixture, result);
    }
    else if (fixture.state == PersistedScanState::Aborted)
    {
        ok = repo.mark_scan_aborted(h, scan_id, result, "2026-01-01 00:00:02");
    }
    else if (fixture.state == PersistedScanState::DependencyMissing)
    {
        ok = repo.mark_scan_dependency_missing(h, scan_id, result, "2026-01-01 00:00:02");
    }
    else
    {
        ok = repo.mark_scan_failed(h, scan_id, result, "2026-01-01 00:00:02");
    }

    if (!ok)
        return -1;

    if (fixture.deleted && !repo.soft_delete_scan_by_id(h, scan_id, "2026-01-01 00:00:03"))
        return -1;

    return scan_id;
    });
}

ScopedFakeNmap::ScopedFakeNmap(const std::string& directory_template,
                               const std::string& output_line,
                               bool keep_running)
    : original_path_(std::getenv("PATH") ? std::getenv("PATH") : "")
{
#ifdef _WIN32
    (void)directory_template;
    dir_ = make_windows_temp_dir_path();
    if (dir_.empty())
        return;

    script_path_ = dir_ + "\\nmap.bat";

    std::ofstream script(script_path_.c_str(), std::ios::out | std::ios::trunc | std::ios::binary);
    if (!script)
        return;

    script << build_windows_fake_nmap_script(output_line, keep_running);
    script.close();

    const std::string path_value = dir_ + ";" + original_path_;
    if (!set_test_env("PATH", path_value))
        return;

    ready_ = true;
#else
    std::vector<char> buffer(directory_template.begin(), directory_template.end());
    buffer.push_back('\0');

    char* dir = mkdtemp(buffer.data());
    if (dir == nullptr)
        return;

    dir_ = dir;
    script_path_ = dir_ + "/nmap";

    std::ofstream script(script_path_.c_str(), std::ios::out | std::ios::trunc);
    if (!script)
        return;

    script << build_fake_nmap_script(output_line, keep_running);
    script.close();

    if (chmod(script_path_.c_str(), 0700) != 0)
        return;

    const std::string path_value = dir_ + ":" + original_path_;
    if (!set_test_env("PATH", path_value))
        return;

    ready_ = true;
#endif
}

ScopedFakeNmap::~ScopedFakeNmap()
{
    if (!dir_.empty())
    {
        std::remove(script_path_.c_str());
#ifdef _WIN32
        RemoveDirectoryA(dir_.c_str());
#else
        rmdir(dir_.c_str());
#endif
    }

    if (!ready_)
        return;

    if (original_path_.empty())
        unset_test_env("PATH");
    else
        set_test_env("PATH", original_path_);
}

bool ScopedFakeNmap::ready() const
{
    return ready_;
}

std::string ScopedFakeNmap::executable_path() const
{
    return script_path_;
}
