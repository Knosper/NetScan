#ifndef APP_STARTUP_VALIDATION_HPP
#define APP_STARTUP_VALIDATION_HPP

#include "app/config.hpp"
#include "scan/nmap.hpp"
#include "util/logger.hpp"

#include <string>
#include <vector>

struct StartupValidationIssue
{
    bool        fatal;
    std::string message;
};

struct StartupValidationResult
{
    bool                             ok;
    std::vector<StartupValidationIssue> issues;
};

typedef NmapCheckResult (*NmapCheckFn)();

StartupValidationResult validate_startup_environment(const AppConfig& config, Logger& logger,
                                                     NmapCheckFn nmap_check_fn = check_nmap);

#endif
