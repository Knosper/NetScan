#ifndef APP_HTTP_BOOTSTRAP_HPP
#define APP_HTTP_BOOTSTRAP_HPP

#include "app/config.hpp"
#include "app/shutdown_controller.hpp"
#include "http/server.hpp"
#include "service/presence_scheduler_service.hpp"
#include "service/scheduler_service.hpp"
#include "service/scan_service.hpp"
#include "util/logger.hpp"
#include <string>

std::string bind_error_message(const std::string& host, int port);

struct ServerRunContext
{
    Server& server;
    ScanService& scan_service;
    SchedulerService& scheduler;
    PresenceSchedulerService& presence_scheduler;
    const AppConfig& config;
    ShutdownController& shutdown;
    Logger& logger;
    std::string config_path;
};

int run_server(ServerRunContext ctx);

#endif
