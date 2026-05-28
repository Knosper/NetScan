#include "app/config.hpp"
#include "app/logging_setup.hpp"
#include "db/database.hpp"
#include "health/health.hpp"
#include "health/health_check.hpp"
#include "http/api_handler.hpp"
#include "httplib/httplib.h"
#include "scan/scan_types.hpp"
#include "test_output.hpp"
#include "util/logger.hpp"

#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{
struct CapturedLine
{
    LogLevel level;
    std::string line;
};

class CapturingSink : public LogSink
{
public:
    void write_line(LogLevel level, const std::string& line)
    {
        lines.push_back({level, line});
    }

    std::vector<CapturedLine> lines;
};

bool expect(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << "\n";
        return false;
    }
    return true;
}

bool contains_line(const std::vector<CapturedLine>& lines, LogLevel level,
                   const std::string& needle)
{
    for (std::vector<CapturedLine>::const_iterator it = lines.begin(); it != lines.end(); ++it)
    {
        if (it->level == level && it->line.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

#ifndef _WIN32
class ScopedEnvVar
{
public:
    explicit ScopedEnvVar(const char* name) : name_(name), had_value_(false)
    {
        const char* current = std::getenv(name_);
        if (current != nullptr)
        {
            had_value_ = true;
            old_value_ = current;
        }
    }

    ~ScopedEnvVar()
    {
        if (had_value_)
            setenv(name_, old_value_.c_str(), 1);
        else
            unsetenv(name_);
    }

    void unset()
    {
        unsetenv(name_);
    }

private:
    const char* name_;
    bool had_value_;
    std::string old_value_;
};

class ScopedTempDir
{
public:
    ScopedTempDir()
    {
        char pattern[] = "/tmp/netscan-db-perms-XXXXXX";
        char* created = mkdtemp(pattern);
        if (created != nullptr)
            path_ = created;
    }

    ~ScopedTempDir()
    {
        if (path_.empty())
            return;

        std::remove((path_ + "/netscan.db-shm").c_str());
        std::remove((path_ + "/netscan.db-wal").c_str());
        std::remove((path_ + "/netscan.db").c_str());
        rmdir(path_.c_str());
    }

    const std::string& path() const { return path_; }

private:
    std::string path_;
};

bool write_mode(const std::string& path, mode_t mode)
{
    return chmod(path.c_str(), mode) == 0;
}

bool expect_mode_0600(const std::string& path, const std::string& label)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0)
        return expect(false, label + " should exist");
    return expect((st.st_mode & 0777) == 0600, label + " should have mode 0600");
}
#endif
} // namespace

int main()
{
    bool all_ok = true;

    {
        Logger logger;
        std::unique_ptr<CapturingSink> sink(new CapturingSink());
        CapturingSink* captured = sink.get();
        logger.add_sink(std::move(sink));

        httplib::Response res;
        handle_api_request(res, ApiRequestContext{"/api/test", logger}, []() -> std::string {
            throw std::runtime_error("boom");
        });

        all_ok = expect(res.status == 500, "API handler should return HTTP 500 on exception") && all_ok;
        all_ok = expect(contains_line(captured->lines, LogLevel::Error, "/api/test failed: boom"),
                        "API handler should log the exception through the injected logger") && all_ok;
    }

    {
        Logger logger;
        std::unique_ptr<CapturingSink> sink(new CapturingSink());
        CapturingSink* captured = sink.get();
        logger.add_sink(std::move(sink));

        Database db(":memory:");
        const HealthResult health = db.read(
            [&logger](sqlite3* h)
            {
                return get_health_result(
                    h, logger,
                    NmapCheckResult{HealthCheckStatus::Error, "PATH environment variable not set"},
                    "127.0.0.1");
            });

        all_ok = expect(health.app_status == AppStatus::Degraded,
                        "health result should degrade app status when nmap check errors") && all_ok;
        all_ok = expect(contains_line(captured->lines, LogLevel::Error,
                                      "Health check: nmap availability check failed: PATH environment variable not set"),
                        "health check should log detailed nmap availability errors") && all_ok;
    }



    {
        Logger logger;
        AppConfig config = make_default_config();
        config.log_level = "error";
        config.log_file = "./__missing_logger_dir__/netscan.log";

        std::ostringstream diagnostics;
        configure_logging(config, logger, diagnostics);

        all_ok = expect(diagnostics.str().find("Failed to open log file: ./__missing_logger_dir__/netscan.log") !=
                            std::string::npos,
                        "startup logging should report file sink failures regardless of log level") &&
                 all_ok;
    }

#ifndef _WIN32
    {
        ScopedTempDir temp_dir;
        all_ok = expect(!temp_dir.path().empty(), "temporary db permissions directory should be created") && all_ok;

        Logger logger;
        const std::string db_path = temp_dir.path() + "/netscan.db";

        {
            Database db(db_path);
            configure_database_runtime(db, logger);
            all_ok = expect(db.write([&db](sqlite3* h)
                            {
                                return db.exec_sql(h,
                                                   "CREATE TABLE IF NOT EXISTS permission_probe (id INTEGER);");
                            }),
                            "database permissions fixture should create a table") && all_ok;
            harden_database_file_permissions(db, logger);

            all_ok = expect_mode_0600(db_path, "netscan.db after fresh startup") && all_ok;
            all_ok = expect_mode_0600(db_path + "-wal", "netscan.db-wal after fresh startup") && all_ok;
            all_ok = expect_mode_0600(db_path + "-shm", "netscan.db-shm after fresh startup") && all_ok;

            all_ok = expect(write_mode(db_path, 0644),
                            "existing netscan.db should be made world-readable for regression fixture") && all_ok;
            all_ok = expect(write_mode(db_path + "-wal", 0644),
                            "existing netscan.db-wal should be made world-readable for regression fixture") && all_ok;
            all_ok = expect(write_mode(db_path + "-shm", 0644),
                            "existing netscan.db-shm should be made world-readable for regression fixture") && all_ok;
        }

        {
            Database db(db_path);
            configure_database_runtime(db, logger);
            harden_database_file_permissions(db, logger);
            all_ok = expect_mode_0600(db_path, "netscan.db after second startup") && all_ok;
            all_ok = expect_mode_0600(db_path + "-wal", "netscan.db-wal after second startup") && all_ok;
            all_ok = expect_mode_0600(db_path + "-shm", "netscan.db-shm after second startup") && all_ok;
        }
    }
#endif

    return finish_test("logger_integration_test", all_ok);
}
