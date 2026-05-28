#include "util/logger.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "test_output.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace
{
struct CapturedLine
{
    LogLevel     level;
    std::string  line;
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

class BlockingSink : public LogSink
{
public:
    BlockingSink(std::atomic<bool>& entered, std::atomic<bool>& release)
        : entered_(entered), release_(release)
    {
    }

    void write_line(LogLevel /*level*/, const std::string& /*line*/)
    {
        entered_.store(true);
        while (!release_.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

private:
    std::atomic<bool>& entered_;
    std::atomic<bool>& release_;
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

#ifdef _WIN32
std::string make_temp_log_path()
{
    char temp_dir[MAX_PATH + 1] = {0};
    const DWORD temp_dir_len = GetTempPathA(MAX_PATH, temp_dir);
    if (temp_dir_len == 0 || temp_dir_len > MAX_PATH)
        return std::string();

    char temp_file[MAX_PATH + 1] = {0};
    if (GetTempFileNameA(temp_dir, "netscan", 0, temp_file) == 0)
        return std::string();

    DeleteFileA(temp_file);
    return std::string(temp_file) + ".log";
}
#else
std::string make_temp_log_path()
{
    char path_template[] = "/tmp/netscan-logger-test-XXXXXX";
    const int fd = mkstemp(path_template);
    if (fd < 0)
        return std::string();

    close(fd);
    return std::string(path_template);
}
#endif
} // namespace

int main()
{
    bool all_ok = true;

    {
        Logger logger;
        logger.set_level(LogLevel::Warn);

        std::unique_ptr<CapturingSink> sink(new CapturingSink());
        CapturingSink* captured = sink.get();
        logger.add_sink(std::move(sink));

        logger.info("should be filtered out");
        logger.warn("warn message");
        logger.error("error message");

        all_ok = expect(captured->lines.size() == 2,
                        "level filtering should keep only warn/error at warn level") && all_ok;
        if (captured->lines.size() == 2)
        {
            all_ok = expect(captured->lines[0].level == LogLevel::Warn,
                            "first captured line should be warn") && all_ok;
            all_ok = expect(captured->lines[1].level == LogLevel::Error,
                            "second captured line should be error") && all_ok;
        }
    }

    {
        Logger logger;

        std::unique_ptr<CapturingSink> sink(new CapturingSink());
        CapturingSink* captured = sink.get();
        logger.add_sink(std::move(sink));

        logger.info("format test message");

        all_ok = expect(captured->lines.size() == 1,
                        "format test should capture one line") && all_ok;
        if (captured->lines.size() == 1)
        {
            all_ok = expect(captured->lines[0].line.find("[INFO] format test message") !=
                                std::string::npos,
                            "log line should include level tag and message") && all_ok;
        }
    }

    {
        Logger logger;

        std::unique_ptr<CapturingSink> sink_a(new CapturingSink());
        std::unique_ptr<CapturingSink> sink_b(new CapturingSink());
        CapturingSink* captured_a = sink_a.get();
        CapturingSink* captured_b = sink_b.get();

        logger.add_sink(std::move(sink_a));
        logger.add_sink(std::move(sink_b));

        logger.info("fanout message");

        all_ok = expect(captured_a->lines.size() == 1,
                        "first sink should receive fanout message") && all_ok;
        all_ok = expect(captured_b->lines.size() == 1,
                        "second sink should receive fanout message") && all_ok;
    }

    {
        const std::string log_path = make_temp_log_path();
        all_ok = expect(!log_path.empty(), "temporary logger file path should be created") && all_ok;

        if (!log_path.empty())
        {
            Logger logger;
            std::unique_ptr<FileLogSink> file_sink(new FileLogSink(log_path));
            all_ok = expect(file_sink->is_open(), "file sink should open temp log file") && all_ok;

            if (file_sink->is_open())
            {
                logger.add_sink(std::move(file_sink));
                logger.info("file sink message");

                std::ifstream file(log_path.c_str());
                std::string content;
                std::getline(file, content);
                all_ok = expect(content.find("[INFO] file sink message") != std::string::npos,
                                "file sink should persist formatted log line") && all_ok;
            }

            std::remove(log_path.c_str());
        }
    }

    {
        Logger logger;
        std::atomic<bool> sink_entered(false);
        std::atomic<bool> release_sink(false);

        std::unique_ptr<BlockingSink> sink(new BlockingSink(sink_entered, release_sink));
        logger.add_sink(std::move(sink));

        std::thread log_thread([&logger]() { logger.info("blocking sink message"); });

        const std::chrono::steady_clock::time_point wait_deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (!sink_entered.load() && std::chrono::steady_clock::now() < wait_deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));

        all_ok = expect(sink_entered.load(),
                        "blocking sink should begin processing the log line") && all_ok;

        const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
        logger.set_level(LogLevel::Error);
        const std::chrono::milliseconds elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);

        all_ok = expect(elapsed < std::chrono::milliseconds(100),
                        "logger configuration should not block on sink flush") && all_ok;

        release_sink.store(true);
        log_thread.join();
    }

#ifndef _WIN32
    {
        Logger logger;
        std::unique_ptr<FileLogSink> file_sink(new FileLogSink("/dev/full"));
        all_ok = expect(file_sink->is_open(), "file sink should open /dev/full for write failure test") &&
                 all_ok;

        if (file_sink->is_open())
        {
            std::ostringstream captured_stderr;
            std::streambuf* const original_stderr = std::cerr.rdbuf(captured_stderr.rdbuf());

            logger.add_sink(std::move(file_sink));
            logger.error("first fallback message");
            logger.error("second fallback message");

            std::cerr.rdbuf(original_stderr);

            const std::string stderr_output = captured_stderr.str();
            all_ok = expect(stderr_output.find("file log sink disabled after write failure") !=
                                std::string::npos,
                            "file sink should emit a one-time stderr warning after write failure") &&
                     all_ok;
            all_ok = expect(stderr_output.find("[ERROR] first fallback message") != std::string::npos,
                            "file sink should mirror the failed log line to stderr") && all_ok;
            all_ok = expect(stderr_output.find("[ERROR] second fallback message") != std::string::npos,
                            "file sink should continue writing later log lines to stderr") && all_ok;
            all_ok = expect(stderr_output.find("file log sink disabled after write failure") ==
                                stderr_output.rfind("file log sink disabled after write failure"),
                            "file sink warning should only be emitted once") && all_ok;
        }
    }
#endif

    return finish_test("logger_test", all_ok);
}
