#include "http/api_setup_reset.hpp"
#include "app/shutdown_controller.hpp"
#include "util/logger.hpp"
#include "util/path_utils.hpp"

#include <cstdio>

// Auth is enforced by the global pre-routing guard in api_guard.cpp;
// no per-route auth check is needed here.
static void handle_setup_reset(httplib::Response& res,
                                const std::string& config_path,
                                Logger& logger)
{
    const std::string absolute_path = to_absolute_path(config_path);
    std::remove(absolute_path.c_str());
    logger.info("Setup reset: deleted config file, requesting restart");
    res.set_content(R"({"status":"ok"})", "application/json");
    ShutdownController::request_restart();
}

void register_setup_reset_route(httplib::Server& svr,
                                 const std::string& config_path,
                                 Logger& logger)
{
    Logger* log = &logger;
    svr.Post("/api/setup/reset",
             [config_path, log](const httplib::Request&, httplib::Response& res)
             { handle_setup_reset(res, config_path, *log); });
}
