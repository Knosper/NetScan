#include "http/routes.hpp"
#include "http/api_handler.hpp"
#include "http/api_dashboard.hpp"
#include "http/api_guard.hpp"
#include "http/api_health_route.hpp"
#include "http/api_host.hpp"
#include "http/api_notes.hpp"
#include "http/api_presence.hpp"
#include "http/api_profiles.hpp"
#include "http/api_scan.hpp"
#include "http/api_scheduler.hpp"
#include "http/api_settings.hpp"
#include "http/api_setup_reset.hpp"
#include "http/api_static.hpp"
#include "http/api_topology.hpp"
#include "app/shutdown_controller.hpp"
#include "http/http_status.hpp"
#include "http/responses.hpp"
#include "http/route_utils.hpp"
#include "util/logger.hpp"

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

static void add_settings_routes(MethodRegistry& reg)
{
    reg.add("/api/settings", "GET");
    reg.add("/api/settings", "POST");
    reg.add("/api/settings/setup", "POST");
    reg.add("/api/setup/reset", "POST");
}

static void add_scan_routes(MethodRegistry& reg)
{
    reg.add("/api/scan/start", "POST");
    reg.add("/api/scan/status", "GET");
    reg.add("/api/scans", "GET");
    reg.add(R"(/api/scans/(\d+))", "GET");
    reg.add(R"(/api/scans/(\d+))", "DELETE");
    reg.add(R"(/api/scan/(\d+)/abort)", "POST");
    reg.add(R"(/api/scans/(\d+)/diff)", "GET");
    reg.add("/api/scan-diff/acknowledgements", "PUT");
    reg.add("/api/scan-diff/acknowledgements", "DELETE");
    reg.add(R"(/api/scans/(\d+)/notes)", "GET");
    reg.add(R"(/api/scans/(\d+)/notes)", "POST");
    reg.add(R"(/api/scans/(\d+)/notes/(\d+))", "DELETE");
    reg.add(R"(/api/scans/(\d+)/topology)", "GET");
}

static void add_host_routes(MethodRegistry& reg)
{
    reg.add("/api/hosts", "GET");
    reg.add("/api/hosts/ports/closed", "DELETE");
    reg.add(R"(/api/hosts/([^/]+))", "GET");
    reg.add(R"(/api/hosts/([^/]+)/meta)", "PATCH");
    reg.add(R"(/api/hosts/([^/]+)/meta/([^/]+))", "DELETE");
}

static void add_profile_routes(MethodRegistry& reg)
{
    reg.add("/api/profiles", "GET");
    reg.add("/api/profiles", "POST");
    reg.add(R"(/api/profiles/(\d+))", "DELETE");
    reg.add(R"(/api/profiles/(\d+))", "PUT");
    reg.add(R"(/api/profiles/(\d+)/run)", "POST");
}

static void add_scheduler_routes(MethodRegistry& reg)
{
    reg.add("/api/scheduler/jobs", "GET");
    reg.add("/api/scheduler/jobs", "POST");
    reg.add(R"(/api/scheduler/jobs/(\d+))", "DELETE");
    reg.add(R"(/api/scheduler/jobs/(\d+))", "PATCH");
}

static void add_presence_routes(MethodRegistry& reg)
{
    reg.add("/api/presence/trackers", "GET");
    reg.add("/api/presence/trackers", "POST");
    reg.add(R"(/api/presence/trackers/(\d+))", "GET");
    reg.add(R"(/api/presence/trackers/(\d+))", "PATCH");
    reg.add(R"(/api/presence/trackers/(\d+))", "DELETE");
    reg.add(R"(/api/presence/trackers/(\d+)/check)", "POST");
    reg.add(R"(/api/presence/trackers/(\d+)/results)", "GET");
}

static MethodRegistry build_method_registry()
{
    MethodRegistry reg;

    reg.add("/api/shutdown", "POST");
    reg.add("/api/health", "GET");
    reg.add("/api/dashboard", "GET");
    add_settings_routes(reg);
    add_scan_routes(reg);
    add_host_routes(reg);
    add_profile_routes(reg);
    add_scheduler_routes(reg);
    add_presence_routes(reg);

    return reg;
}

static void handle_shutdown_request(httplib::Response& res, Logger& logger)
{
    handle_api_action(res, ApiRequestContext{"POST /api/shutdown", logger},
                      []()
                      {
                          ShutdownController::request();
                          nlohmann::json body;
                          body["status"] = "shutdown_requested";
                          return body.dump();
                      });
}

static void register_api_routes(httplib::Server& svr, ApiServices& services, Logger& logger,
                                bool ui_enabled)
{
    Logger* log = &logger;
    svr.Post("/api/shutdown",
             [log](const httplib::Request&, httplib::Response& res)
             { handle_shutdown_request(res, *log); });

    register_health_route(svr, services.health_svc, logger, ui_enabled);
    register_dashboard_routes(svr, services.dashboard_svc, logger);
    register_host_routes(svr, services.host_svc, services.settings_svc, logger);
    register_notes_routes(svr, services.note_svc, logger);
    register_presence_routes(svr, services.presence_svc, logger);
    register_profile_routes(svr, services.profile_svc);
    register_scan_routes(svr, ScanRouteServices{services.scan_svc, services.scan_diff_svc,
                                                services.scan_history_svc,
                                                services.settings_svc, logger});
    register_scheduler_routes(svr, services.scheduler_svc);
    register_settings_routes(svr, services.settings_svc, logger);
    register_topology_routes(svr, services.topology_svc, logger);
}

void register_routes(httplib::Server& svr, const AppContext& ctx, const AuthConfig& auth_config,
                     ApiServices& services)
{
    // Keep httplib's transport cap above the API JSON cap so routes can return
    // a structured 413 response instead of dropping oversized requests.
    svr.set_payload_max_length(max_json_request_body_bytes() + (16U * 1024U));
    register_api_key_guard(svr, ctx.config, auth_config);
    register_api_response_headers(svr, ctx.config);
    register_api_routes(svr, services, ctx.logger, ctx.config.ui_enabled);
    register_setup_reset_route(svr, ctx.config_path, ctx.logger);
    register_error_handlers(svr, build_method_registry());
    if (ctx.config.ui_enabled)
        register_static_routes(svr, ctx);
    else
        svr.Get("/", [](const httplib::Request&, httplib::Response& res)
                { set_json_response(res, 200, R"({"status":"ok","ui_enabled":false})"); });
}
