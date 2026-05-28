#include "app/startup_validation.hpp"
#include "test_output.hpp"
#include "util/path_utils.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define MKDIR(path) _mkdir(path)
#define RMDIR(path) _rmdir(path)
#else
#include <sys/stat.h>
#include <unistd.h>
#define MKDIR(path) mkdir(path, 0700)
#define RMDIR(path) rmdir(path)
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

bool contains_line(const std::vector<CapturedLine>& lines, LogLevel level, const std::string& text)
{
    for (std::vector<CapturedLine>::const_iterator it = lines.begin(); it != lines.end(); ++it)
    {
        if (it->level == level && it->line.find(text) != std::string::npos)
            return true;
    }
    return false;
}

std::string make_temp_base_path()
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
    return std::string(temp_file);
#else
    std::string path = "/tmp/netscan-startup-validation-XXXXXX";
    std::vector<char> buffer(path.begin(), path.end());
    buffer.push_back('\0');
    char* dir = mkdtemp(buffer.data());
    if (dir == nullptr)
        return std::string();
    RMDIR(dir);
    return std::string(dir);
#endif
}

bool write_file(const std::string& path, const std::string& content)
{
    std::ofstream file(path.c_str(), std::ios::out | std::ios::trunc | std::ios::binary);
    if (!file.is_open())
        return false;
    file << content;
    return file.good();
}

class ScopedTestLayout
{
public:
    ScopedTestLayout() : base_(make_temp_base_path())
    {
        if (base_.empty())
            return;
        root_ = base_;
        web_dir_ = join_path(root_, "web");
        db_dir_ = join_path(root_, "db");
    }

    ~ScopedTestLayout()
    {
        if (!web_dir_.empty())
            std::remove(join_path(web_dir_, "index.html").c_str());
        if (!db_path_.empty())
            std::remove(db_path_.c_str());
        if (!web_dir_.empty())
            RMDIR(web_dir_.c_str());
        if (!db_dir_.empty())
            RMDIR(db_dir_.c_str());
        if (!root_.empty())
            RMDIR(root_.c_str());
    }

    bool create_root()
    {
        return !root_.empty() && MKDIR(root_.c_str()) == 0;
    }

    bool create_web_dir()
    {
        return MKDIR(web_dir_.c_str()) == 0;
    }

    bool create_db_dir()
    {
        return MKDIR(db_dir_.c_str()) == 0;
    }

    bool create_index()
    {
        return write_file(join_path(web_dir_, "index.html"), "<!doctype html>\n");
    }

    bool create_web_file_instead_of_dir()
    {
        return write_file(web_dir_, "not a directory");
    }

    std::string root() const { return root_; }
    std::string web_dir() const { return web_dir_; }
    std::string db_dir() const { return db_dir_; }

    std::string db_path()
    {
        db_path_ = join_path(db_dir_, "netscan.db");
        return db_path_;
    }

private:
    std::string base_;
    std::string root_;
    std::string web_dir_;
    std::string db_dir_;
    std::string db_path_;
};

AppConfig make_config(const std::string& web_dir, const std::string& db_path,
                      bool ui_enabled = true)
{
    AppConfig config = make_default_config();
    config.web_dir = web_dir;
    config.db_path = db_path;
    config.ui_enabled = ui_enabled;
    return config;
}

NmapCheckResult nmap_ok()
{
    return {HealthCheckStatus::Ok, ""};
}

NmapCheckResult nmap_missing()
{
    return {HealthCheckStatus::Missing, ""};
}

NmapCheckResult nmap_error()
{
    return {HealthCheckStatus::Error, "probe failed"};
}
} // namespace

int main()
{
    bool all_ok = true;

    {
        ScopedTestLayout layout;
        all_ok = expect(layout.create_root(), "test root should be created") && all_ok;
        all_ok = expect(layout.create_db_dir(), "db dir should be created") && all_ok;

        Logger logger;
        StartupValidationResult result =
            validate_startup_environment(make_config(layout.web_dir(), layout.db_path()), logger, nmap_ok);

        all_ok = expect(!result.ok, "missing web_dir should fail startup validation") && all_ok;
    }

    {
        ScopedTestLayout layout;
        all_ok = expect(layout.create_root(), "test root should be created for file web path") && all_ok;
        all_ok = expect(layout.create_db_dir(), "db dir should be created for file web path") && all_ok;
        all_ok = expect(layout.create_web_file_instead_of_dir(),
                        "web file fixture should be created") && all_ok;

        Logger logger;
        StartupValidationResult result =
            validate_startup_environment(make_config(layout.web_dir(), layout.db_path()), logger, nmap_ok);

        all_ok = expect(!result.ok, "file web_dir should fail startup validation") && all_ok;
    }

    {
        ScopedTestLayout layout;
        all_ok = expect(layout.create_root(), "test root should be created for missing index") && all_ok;
        all_ok = expect(layout.create_db_dir(), "db dir should be created for missing index") && all_ok;
        all_ok = expect(layout.create_web_dir(), "web dir should be created for missing index") && all_ok;

        Logger logger;
        StartupValidationResult result =
            validate_startup_environment(make_config(layout.web_dir(), layout.db_path()), logger, nmap_ok);

        all_ok = expect(!result.ok, "missing index.html should fail startup validation") && all_ok;
    }

    {
        ScopedTestLayout layout;
        all_ok = expect(layout.create_root(), "test root should be created for missing db dir") && all_ok;
        all_ok = expect(layout.create_web_dir(), "web dir should be created for missing db dir") && all_ok;
        all_ok = expect(layout.create_index(), "index fixture should be created") && all_ok;

        Logger logger;
        const std::string missing_db_dir = join_path(layout.root(), "missing") + "/netscan.db";
        StartupValidationResult result =
            validate_startup_environment(make_config(layout.web_dir(), missing_db_dir), logger, nmap_ok);

        all_ok = expect(!result.ok, "missing database parent directory should fail startup validation") &&
                 all_ok;
    }

    {
        ScopedTestLayout layout;
        all_ok = expect(layout.create_root(), "test root should be created for success path") && all_ok;
        all_ok = expect(layout.create_web_dir(), "web dir should be created for success path") && all_ok;
        all_ok = expect(layout.create_db_dir(), "db dir should be created for success path") && all_ok;
        all_ok = expect(layout.create_index(), "index fixture should be created for success path") && all_ok;

        Logger logger;
        std::unique_ptr<CapturingSink> sink(new CapturingSink());
        CapturingSink* captured = sink.get();
        logger.add_sink(std::move(sink));

        StartupValidationResult result =
            validate_startup_environment(make_config(layout.web_dir(), layout.db_path()), logger, nmap_missing);

        all_ok = expect(result.ok, "missing nmap should not fail startup validation") && all_ok;
        all_ok = expect(result.issues.size() == 1 && !result.issues[0].fatal,
                        "missing nmap should produce one non-fatal degraded issue") && all_ok;
        all_ok = expect(contains_line(captured->lines, LogLevel::Warn, "nmap not found in PATH"),
                        "missing nmap should be logged as startup warning") && all_ok;
        all_ok = expect(contains_line(captured->lines, LogLevel::Info, "Startup validation: ui_enabled="),
                        "startup validation should log ui_enabled") && all_ok;
    }

    {
        ScopedTestLayout layout;
        all_ok = expect(layout.create_root(), "test root should be created for nmap error path") && all_ok;
        all_ok = expect(layout.create_web_dir(), "web dir should be created for nmap error path") && all_ok;
        all_ok = expect(layout.create_db_dir(), "db dir should be created for nmap error path") && all_ok;
        all_ok = expect(layout.create_index(), "index fixture should be created for nmap error path") &&
                 all_ok;

        Logger logger;
        StartupValidationResult result =
            validate_startup_environment(make_config(layout.web_dir(), layout.db_path()), logger, nmap_error);

        all_ok = expect(result.ok, "nmap probe errors should not fail startup validation") && all_ok;
        all_ok = expect(result.issues.size() == 1 && !result.issues[0].fatal,
                        "nmap probe errors should stay non-fatal") && all_ok;
    }

    {
        ScopedTestLayout layout;
        all_ok = expect(layout.create_root(), "test root should be created for api-only path") && all_ok;
        all_ok = expect(layout.create_db_dir(), "db dir should be created for api-only path") && all_ok;

        Logger logger;
        StartupValidationResult result =
            validate_startup_environment(make_config(layout.web_dir(), layout.db_path(), false), logger, nmap_ok);

        all_ok = expect(result.ok, "api-only mode should pass without web_dir") && all_ok;
    }

    return finish_test("startup_validation_test", all_ok);
}
