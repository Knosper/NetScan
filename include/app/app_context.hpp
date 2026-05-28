#ifndef APP_CONTEXT_HPP
#define APP_CONTEXT_HPP

#include "config.hpp"
#include "util/logger.hpp"
#include <string>

struct AppContext
{
    const AppConfig& config;
    Logger& logger;
    std::string config_path = "";
};

#endif
