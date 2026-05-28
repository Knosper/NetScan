#include "app/logging_setup.hpp"

#include "util/logger.hpp"

#include <iostream>
#include <memory>

void configure_logging(const AppConfig& config, Logger& logger, std::ostream& diagnostics)
{
    logger.set_level(parse_log_level(config.log_level));

    if (config.log_file.empty())
        return;

    std::unique_ptr<FileLogSink> file_sink(new FileLogSink(config.log_file));
    if (file_sink->is_open())
    {
        logger.add_sink(std::move(file_sink));
        return;
    }

    diagnostics << "Failed to open log file: " << config.log_file;
    if (!file_sink->failure_reason().empty())
        diagnostics << ": " << file_sink->failure_reason();
    diagnostics << "\n";
}
