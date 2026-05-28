#ifndef APP_LOGGING_SETUP_HPP
#define APP_LOGGING_SETUP_HPP

#include "app/config.hpp"
#include "util/logger.hpp"
#include <iosfwd>

void configure_logging(const AppConfig& config, Logger& logger, std::ostream& diagnostics);

#endif
