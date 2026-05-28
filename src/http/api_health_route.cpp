#include "http/api_health_route.hpp"
#include "http/api_handler.hpp"
#include "http/http_status.hpp"
#include "http/responses.hpp"

#include <nlohmann/json.hpp>

static nlohmann::json health_to_json(const HealthResult& health, bool ui_enabled)
{
    nlohmann::json body;
    body["status"]     = to_string(health.app_status);
    body["checks"]     = {{"database", to_string(health.database_status)},
                          {"nmap", to_string(health.nmap_status)}};
    body["message"]    = health.message;
    body["ui_enabled"] = ui_enabled;
    body["local_ip"]   = health.local_ip;
    body["mode"]       = "normal";
    return body;
}

static void handle_health_request(httplib::Response& res, HealthService& service, Logger& logger,
                                   bool ui_enabled)
{
    handle_api_request(res, ApiRequestContext{"GET /api/health", logger},
                       [&service, ui_enabled]()
                       { return health_to_json(service.get_health(), ui_enabled).dump(); });
}

void register_health_route(httplib::Server& svr, HealthService& service, Logger& logger,
                            bool ui_enabled)
{
    HealthService* svc = &service;
    Logger*        log = &logger;
    svr.Get("/api/health",
            [svc, log, ui_enabled](const httplib::Request&, httplib::Response& res)
            { handle_health_request(res, *svc, *log, ui_enabled); });
}
