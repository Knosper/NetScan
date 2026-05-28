#ifndef APP_APPLICATION_BOOTSTRAP_HPP
#define APP_APPLICATION_BOOTSTRAP_HPP

#include "app/config.hpp"
#include "util/logger.hpp"
#include <string>

// Returned when the process needs to restart (e.g. after browser-based setup).
// AppRun and shell wrappers loop on this exit code.
static const int EXIT_RESTART = 75;

struct GenerateCertOptions
{
    std::string ip     = "127.0.0.1";
    bool        ip_set = false;
    int         days   = 365;
    bool        force  = false;
};

class ApplicationBootstrap
{
public:
    int run(const std::string& config_path, const CliOverrides& overrides, Logger& logger);
    int run_setup(const std::string& config_path, Logger& logger);
    int run_setup_server(const std::string& config_path, Logger& logger);
    int generate_key(const std::string& config_path, const std::string& key_name, Logger& logger);
    int generate_cert(const std::string& config_path, const GenerateCertOptions& opts, Logger& logger);
};

#endif
