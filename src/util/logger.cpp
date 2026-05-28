#include "util/logger.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace
{
std::string current_timestamp()
{
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

const char* level_tag(LogLevel level)
{
    switch (level)
    {
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warn:
        return "WARN";
    case LogLevel::Error:
        return "ERROR";
    }
    return "INFO";
}

bool should_log(LogLevel level, LogLevel threshold)
{
    return level >= threshold;
}

std::string format_log_line(LogLevel level, const std::string& message)
{
    return current_timestamp() + " [" + level_tag(level) + "] " + message;
}
} // namespace

void LogSink::write(LogLevel level, const std::string& line)
{
    std::lock_guard<std::mutex> lock(write_mutex_);
    write_line(level, line);
}

void ConsoleLogSink::write_line(LogLevel level, const std::string& line)
{
    if (level == LogLevel::Warn || level == LogLevel::Error)
        std::cerr << line << "\n";
    else
        std::cout << line << "\n";
}

FileLogSink::FileLogSink(const std::string& path)
    : write_failed_(false), fallback_warning_emitted_(false)
{
    file_.open(path, std::ios::app);
    if (!file_.is_open())
        failure_reason_ = std::strerror(errno);
}

void FileLogSink::write_line(LogLevel /*level*/, const std::string& line)
{
    if (!file_.is_open() || write_failed_)
    {
        if (!fallback_warning_emitted_)
        {
            std::cerr << current_timestamp()
                      << " [ERROR] file log sink disabled after write failure"
                      << (failure_reason_.empty() ? "" : ": " + failure_reason_)
                      << "\n";
            fallback_warning_emitted_ = true;
        }
        std::cerr << line << "\n";
        return;
    }

    file_ << line << "\n";
    file_.flush();
    if (file_.good())
        return;

    failure_reason_ = "log file write failed";
    write_failed_ = true;

    if (!fallback_warning_emitted_)
    {
        std::cerr << current_timestamp()
                  << " [ERROR] file log sink disabled after write failure: "
                  << failure_reason_ << "\n";
        fallback_warning_emitted_ = true;
    }
    std::cerr << line << "\n";
}

bool FileLogSink::is_open() const
{
    return file_.is_open();
}

const std::string& FileLogSink::failure_reason() const
{
    return failure_reason_;
}

Logger::Logger() : level_(LogLevel::Info) {}

void Logger::set_level(LogLevel level)
{
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
}

LogLevel Logger::level() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return level_;
}

void Logger::add_sink(std::unique_ptr<LogSink> sink)
{
    if (!sink)
        return;

    std::lock_guard<std::mutex> lock(mutex_);
    sinks_.push_back(std::move(sink));
}

void Logger::log(LogLevel level, const std::string& message)
{
    LogLevel current_level = LogLevel::Info;
    std::vector<LogSink*> sinks;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        current_level = level_;
        for (std::vector<std::unique_ptr<LogSink>>::iterator it = sinks_.begin();
             it != sinks_.end(); ++it)
        {
            sinks.push_back(it->get());
        }
    }

    if (!should_log(level, current_level))
        return;

    const std::string line = format_log_line(level, message);
    for (std::vector<LogSink*>::iterator it = sinks.begin(); it != sinks.end(); ++it)
    {
        (*it)->write(level, line);
    }
}

void Logger::debug(const std::string& message)
{
    log(LogLevel::Debug, message);
}

void Logger::info(const std::string& message)
{
    log(LogLevel::Info, message);
}

void Logger::warn(const std::string& message)
{
    log(LogLevel::Warn, message);
}

void Logger::error(const std::string& message)
{
    log(LogLevel::Error, message);
}

LogLevel parse_log_level(const std::string& value)
{
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (lower == "debug") return LogLevel::Debug;
    if (lower == "info")  return LogLevel::Info;
    if (lower == "warn")  return LogLevel::Warn;
    if (lower == "error") return LogLevel::Error;
    return LogLevel::Info;
}

bool is_supported_log_level(const std::string& value)
{
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return lower == "debug" || lower == "info" || lower == "warn" || lower == "error";
}
