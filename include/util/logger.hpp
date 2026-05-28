#ifndef UTIL_LOGGER_HPP
#define UTIL_LOGGER_HPP

#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

enum class LogLevel
{
    Debug,
    Info,
    Warn,
    Error
};

class LogSink
{
public:
    virtual ~LogSink() {}
    void write(LogLevel level, const std::string& line);

protected:
    virtual void write_line(LogLevel level, const std::string& line) = 0;

private:
    std::mutex write_mutex_;
};

class ConsoleLogSink : public LogSink
{
public:
    void write_line(LogLevel level, const std::string& line) override;
};

class FileLogSink : public LogSink
{
public:
    explicit FileLogSink(const std::string& path);
    void write_line(LogLevel level, const std::string& line) override;
    bool is_open() const;
    const std::string& failure_reason() const;

private:
    std::ofstream file_;
    std::string   failure_reason_;
    bool          write_failed_;
    bool          fallback_warning_emitted_;
};

class Logger
{
public:
    Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void set_level(LogLevel level);
    LogLevel level() const;

    void add_sink(std::unique_ptr<LogSink> sink);

    void log(LogLevel level, const std::string& message);
    void debug(const std::string& message);
    void info(const std::string& message);
    void warn(const std::string& message);
    void error(const std::string& message);

private:
    LogLevel                              level_;
    mutable std::mutex                    mutex_;
    std::vector<std::unique_ptr<LogSink>> sinks_;
};

LogLevel parse_log_level(const std::string& value);
bool is_supported_log_level(const std::string& value);

#endif
