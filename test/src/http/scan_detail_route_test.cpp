#include "app/app_context.hpp"
#include "app/config.hpp"
#include "db/database.hpp"
#include "db/schema.hpp"
#include "db/scan_repository.hpp"
#include "http/routes.hpp"
#include "service/dashboard_service.hpp"
#include "service/health_service.hpp"
#include "service/host_service.hpp"
#include "service/note_service.hpp"
#include "service/presence_service.hpp"
#include "service/scan_diff_service.hpp"
#include "service/scan_history_service.hpp"
#include "service/scan_service.hpp"
#include "service/settings_service.hpp"
#include "service/topology_service.hpp"
#include "scan_service_test_support.hpp"
#include "test_output.hpp"
#include "util/logger.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{
struct ErrorRouteCase
{
    std::string path;
    int         expected_status;
    std::string label;
    std::string error_type;
    std::string error_message;
};

bool expect_scan_summary_json(const nlohmann::json& body, int id, const std::string& target,
                              const std::string& state)
{
    bool ok = true;
    ok = expect(body.is_object(), "scan detail response should be a JSON object") && ok;
    ok = expect(body.value("id", -1) == id, "scan detail should include the scan id") && ok;
    ok = expect(body.value("target", std::string()) == target,
                "scan detail should include the target") &&
         ok;
    ok = expect(body.value("state", std::string()) == state,
                "scan detail should include the state") &&
         ok;
    ok = expect(body.contains("deleted") && body["deleted"].is_boolean(),
                "scan detail should include the deleted flag") &&
         ok;
    ok = expect(body.contains("createdAt"), "scan detail should include createdAt") && ok;
    ok = expect(body.contains("message"), "scan detail should include message") && ok;
    ok = expect(body.contains("command"), "scan detail should include command") && ok;
    ok = expect(body.contains("exitCode"), "scan detail should include exitCode") && ok;
    ok = expect(body.contains("portCoverageKnown") && body["portCoverageKnown"].is_boolean(),
                "scan detail should include portCoverageKnown") &&
         ok;
    return ok;
}

bool expect_api_error_body(const nlohmann::json& body, const std::string& type,
                           const std::string& message)
{
    bool ok = true;
    ok = expect(body.is_object(), "error response should be a JSON object") && ok;
    ok = expect(body.value("status", std::string()) == "error",
                "error response should set status=error") &&
         ok;
    ok = expect(body.value("type", std::string()) == type,
                "error response should include the expected error type") &&
         ok;
    ok = expect(body.value("message", std::string()) == message,
                "error response should include the expected error message") &&
         ok;
    return ok;
}

bool expect_dashboard_response_shape(const nlohmann::json& body)
{
    bool ok = true;
    ok = expect(body.is_object(), "dashboard response should be a JSON object") && ok;
    ok = expect(body.contains("stats") && body["stats"].is_object(),
                "dashboard response should include stats object") &&
         ok;
    ok = expect(body.contains("scans") && body["scans"].is_array(),
                "dashboard response should include scans array") &&
         ok;
    ok = expect(body.contains("hosts") && body["hosts"].is_array(),
                "dashboard response should include hosts array") &&
         ok;
    ok = expect(body.contains("ports") && body["ports"].is_array(),
                "dashboard response should include ports array") &&
         ok;
    if (!body.contains("stats") || !body["stats"].is_object())
        return false;

    ok = expect(body["stats"].contains("hosts"), "dashboard stats should include hosts") && ok;
    ok = expect(body["stats"].contains("ports"), "dashboard stats should include ports") && ok;
    ok = expect(body["stats"].contains("services"), "dashboard stats should include services") &&
         ok;
    ok = expect(body["stats"].contains("lastScan"), "dashboard stats should include lastScan") &&
         ok;
    return ok;
}

bool expect_health_response_shape(const nlohmann::json& body)
{
    bool ok = true;
    ok = expect(body.is_object(), "health response should be a JSON object") && ok;
    ok = expect(body.contains("status") && body["status"].is_string(),
                "health response should include status string") &&
         ok;
    ok = expect(body.contains("checks") && body["checks"].is_object(),
                "health response should include checks object") &&
         ok;
    ok = expect(body.contains("message") && body["message"].is_string(),
                "health response should include message string") &&
         ok;
    if (!body.contains("checks") || !body["checks"].is_object())
        return false;

    ok = expect(body["checks"].contains("database") && body["checks"]["database"].is_string(),
                "health response should include database check string") &&
         ok;
    ok = expect(body["checks"].contains("nmap") && body["checks"]["nmap"].is_string(),
                "health response should include nmap check string") &&
         ok;
    return ok;
}

} // namespace

int main()
{
    bool all_ok = true;

    const std::string db_path = make_temp_db_path("/tmp/netscan-scan-detail-route-test-XXXXXX.db");
    all_ok = expect(!db_path.empty(), "temporary database path should be created") && all_ok;

    if (!db_path.empty())
    {
        Database db(db_path);
        Logger logger;
        configure_database_runtime(db, logger);
        init_schema(db, logger);

        ScanRepository repo(db);
        ScanService scan_service(db, logger);

        const ScanRequest completed_request = {"127.0.0.1", "443", false, ""};
        const ScanRequest deleted_request = {"127.0.0.2", "22", false, ""};

        const int completed_scan_id = persist_scan(
            db, repo,
            {completed_request, PersistedScanState::Completed, false,
             lsm::scan::ScanSnapshot{{{"127.0.0.1", "localhost", {{"tcp", 443, "open", "https"}}}}}});
        all_ok = expect(completed_scan_id > 0, "completed scan should persist") && all_ok;

        const int diff_scan_id = persist_scan(
            db, repo,
            {completed_request, PersistedScanState::Completed, false,
             lsm::scan::ScanSnapshot{{{"127.0.0.2", "new-host", {{"tcp", 8443, "open", "https-alt"}}}}}});
        all_ok = expect(diff_scan_id > 0, "diff scan should persist") && all_ok;

        const int deleted_scan_id = persist_scan(
            db, repo,
            {deleted_request, PersistedScanState::Completed, false,
             lsm::scan::ScanSnapshot{{{"127.0.0.2", "deleted-host", {{"tcp", 22, "open", "ssh"}}}}},
             true});
        all_ok = expect(deleted_scan_id > 0, "deleted scan should persist") && all_ok;

        {
            std::unique_ptr<PersistedScanSummary> scan = scan_service.get_scan(completed_scan_id);
            all_ok = expect(scan != nullptr, "get_scan should return an existing scan") && all_ok;
            if (scan)
            {
                all_ok = expect(scan->id == completed_scan_id,
                                "get_scan should return the requested scan id") &&
                         all_ok;
                all_ok = expect(scan->target == completed_request.target,
                                "get_scan should preserve the target") &&
                         all_ok;
            }
        }

        all_ok = expect(scan_service.get_scan(9999) == nullptr,
                        "get_scan should return null for a missing scan") &&
                 all_ok;
        all_ok = expect(scan_service.get_scan(deleted_scan_id) == nullptr,
                        "get_scan should hide deleted scans") &&
                 all_ok;

        const TestRepoPaths repo_paths = make_test_repo_paths(__FILE__);
        AppConfig config = make_default_config();
        config.web_dir = repo_paths.web_dir;

        AppContext ctx{config, logger};
        DashboardService dashboard_service(db);
        HealthService health_service(db, logger, "127.0.0.1");
        HostService host_service(db);
        NoteService note_service(db, logger);
        PresenceService presence_service(db, logger);
        SettingsService settings_service(db, repo_paths.conf_path, logger);
        ProfileService profile_service(db, scan_service);
        ScanDiffService diff_service(db, repo);
        ScanHistoryService history_service(db, repo);
        SchedulerService scheduler_service(db, scan_service, logger);
        TopologyService topology_service(db, repo);
        ApiServices services{dashboard_service, health_service, host_service, note_service,
                             presence_service, profile_service, diff_service, history_service,
                             scan_service, scheduler_service, settings_service, topology_service};

        httplib::Server server;
        const AuthConfig auth_config;
        register_routes(server, ctx, auth_config, services);
        int port = -1;
        port = server.bind_to_any_port("127.0.0.1");
        all_ok = expect(port > 0, "test server should bind to an ephemeral port") && all_ok;

        if (port > 0)
        {
            std::thread server_thread([&server]()
                                      { server.listen_after_bind(); });
            httplib::Client client("127.0.0.1", port);
            configure_test_client(client);

            {
                httplib::Result result = client.Get("/api/dashboard");
                all_ok = expect_response_status(result, 200, "GET /api/dashboard") && all_ok;
                if (result)
                {
                    const nlohmann::json body = nlohmann::json::parse(result->body);
                    all_ok = expect_dashboard_response_shape(body) && all_ok;
                }
            }

            {
                httplib::Result result = client.Get("/api/health");
                all_ok = expect_response_status(result, 200, "GET /api/health") && all_ok;
                if (result)
                {
                    const nlohmann::json body = nlohmann::json::parse(result->body);
                    all_ok = expect_health_response_shape(body) && all_ok;
                }
            }

            {
                httplib::Result result =
                    client.Get(("/api/scans/" + std::to_string(completed_scan_id)).c_str());
                all_ok = expect_response_status(result, 200, "GET /api/scans/:id for existing scan") &&
                         all_ok;
                if (result)
                {
                    const nlohmann::json body = nlohmann::json::parse(result->body);
                    all_ok = expect_scan_summary_json(body, completed_scan_id,
                                                      completed_request.target, "completed") &&
                             all_ok;
                }
            }

            {
                const std::string diff_path =
                    "/api/scans/" + std::to_string(diff_scan_id) + "/diff";
                httplib::Result result = client.Get(diff_path.c_str());
                all_ok = expect_response_status(result, 200, "GET /api/scans/:id/diff") &&
                         all_ok;
                if (result)
                {
                    const nlohmann::json body = nlohmann::json::parse(result->body);
                    all_ok = expect(body["newHosts"][0].value("acknowledged", true) == false,
                                    "diff entry should default to unacknowledged") &&
                             all_ok;
                    all_ok = expect(body["newOpenPorts"][0].contains("acknowledgementKey"),
                                    "diff entry should include acknowledgementKey") &&
                             all_ok;
                }

                const std::string ack_body =
                    R"({"category":"new_open_port","ip":"127.0.0.2","port":8443})";
                const std::string host_ack_body =
                    R"({"category":"new_host","ip":"127.0.0.2"})";
                result = client.Put("/api/scan-diff/acknowledgements", ack_body,
                                    "application/json");
                all_ok = expect_response_status(result, 200,
                                                "PUT /api/scan-diff/acknowledgements") &&
                         all_ok;
                result = client.Put("/api/scan-diff/acknowledgements", host_ack_body,
                                    "application/json");
                all_ok = expect_response_status(result, 200,
                                                "PUT host /api/scan-diff/acknowledgements") &&
                         all_ok;

                result = client.Get(diff_path.c_str());
                all_ok = expect_response_status(result, 200,
                                                "GET /api/scans/:id/diff after ack") &&
                         all_ok;
                if (result)
                {
                    const nlohmann::json body = nlohmann::json::parse(result->body);
                    all_ok = expect(body["newHosts"][0].value("acknowledged", false),
                                    "acknowledged host diff entry should be exposed") &&
                             all_ok;
                    all_ok = expect(body["newOpenPorts"][0].value("acknowledged", false),
                                    "acknowledged diff entry should be exposed") &&
                             all_ok;
                }

                result = client.Delete("/api/scan-diff/acknowledgements", ack_body,
                                       "application/json");
                all_ok = expect_response_status(result, 200,
                                                "DELETE /api/scan-diff/acknowledgements") &&
                         all_ok;
                result = client.Delete("/api/scan-diff/acknowledgements", host_ack_body,
                                       "application/json");
                all_ok = expect_response_status(result, 200,
                                                "DELETE host /api/scan-diff/acknowledgements") &&
                         all_ok;

                result = client.Get(diff_path.c_str());
                all_ok = expect_response_status(result, 200,
                                                "GET /api/scans/:id/diff after unack") &&
                         all_ok;
                if (result)
                {
                    const nlohmann::json body = nlohmann::json::parse(result->body);
                    all_ok = expect(!body["newOpenPorts"][0].value("acknowledged", true),
                                    "unacknowledged diff entry should be exposed") &&
                             all_ok;
                    all_ok = expect(!body["newHosts"][0].value("acknowledged", true),
                                    "unacknowledged host diff entry should be exposed") &&
                             all_ok;
                }

                const std::string invalid_category_body =
                    R"({"category":"other","ip":"127.0.0.2"})";
                result = client.Put("/api/scan-diff/acknowledgements",
                                    invalid_category_body, "application/json");
                all_ok = expect_response_status(result, 400,
                                                "PUT acknowledgement invalid category") &&
                         all_ok;

                const std::string missing_ip_body = R"({"category":"new_host"})";
                result = client.Put("/api/scan-diff/acknowledgements",
                                    missing_ip_body, "application/json");
                all_ok = expect_response_status(result, 400,
                                                "PUT acknowledgement missing ip") &&
                         all_ok;

                const std::string missing_port_body =
                    R"({"category":"new_open_port","ip":"127.0.0.2"})";
                result = client.Put("/api/scan-diff/acknowledgements",
                                    missing_port_body, "application/json");
                all_ok = expect_response_status(result, 400,
                                                "PUT acknowledgement missing port") &&
                         all_ok;

                const std::string invalid_port_body =
                    R"({"category":"new_open_port","ip":"127.0.0.2","port":70000})";
                result = client.Put("/api/scan-diff/acknowledgements",
                                    invalid_port_body, "application/json");
                all_ok = expect_response_status(result, 400,
                                                "PUT acknowledgement invalid port") &&
                         all_ok;
            }

            const ErrorRouteCase error_cases[] = {
                {"/api/scans/not-a-number",
                 404,
                 "GET /api/scans/:id for invalid text id",
                 "not_found",
                 "API endpoint not found"},
                {"/api/scans/0",
                 400,
                 "GET /api/scans/:id for zero id",
                 "bad_request",
                 "invalid scan id"},
                {"/api/scans/9999",
                 404,
                 "GET /api/scans/:id for missing scan",
                 "not_found",
                 "scan not found"},
                {"/api/scans/2147483648",
                 400,
                 "GET /api/scans/:id for overflowing id",
                 "bad_request",
                 "invalid scan id"},
            };
            for (const ErrorRouteCase& test_case : error_cases)
            {
                httplib::Result result = client.Get(test_case.path.c_str());
                all_ok =
                    expect_response_status(result, test_case.expected_status, test_case.label) &&
                    all_ok;
                if (result)
                {
                    const nlohmann::json body = nlohmann::json::parse(result->body);
                    all_ok = expect_api_error_body(body, test_case.error_type,
                                                   test_case.error_message) &&
                             all_ok;
                }
            }

            {
                bool saw_rate_limit = false;
                const std::string burst_path =
                    "/api/scans/" + std::to_string(completed_scan_id);
                std::vector<int> statuses(64, 0);
                std::vector<std::string> bodies(statuses.size());
                std::vector<std::thread> workers;
                workers.reserve(statuses.size());

                for (std::size_t i = 0; i < statuses.size(); ++i)
                {
                    workers.emplace_back([port, &burst_path, &statuses, &bodies, i]()
                    {
                        httplib::Client burst_client("127.0.0.1", port);
                        configure_test_client(burst_client);
                        httplib::Result result = burst_client.Get(burst_path.c_str());
                        if (!result)
                        {
                            statuses[i] = -1;
                            return;
                        }
                        statuses[i] = result->status;
                        bodies[i]   = result->body;
                    });
                }

                for (std::thread& worker : workers)
                    worker.join();

                for (std::size_t i = 0; i < statuses.size(); ++i)
                {
                    all_ok = expect(statuses[i] != -1,
                                    "GET scan detail burst request should return") &&
                             all_ok;
                    if (statuses[i] == 429)
                    {
                        saw_rate_limit = true;
                        const nlohmann::json body = nlohmann::json::parse(bodies[i]);
                        all_ok = expect_api_error_body(body, "too_many_requests",
                                                       "rate limit exceeded") &&
                                 all_ok;
                        break;
                    }
                }
                all_ok = expect(saw_rate_limit,
                                "loopback API burst should hit rate limiting with status 429") &&
                         all_ok;
            }

            server.stop();
            server_thread.join();
        }
    }

    if (!db_path.empty())
        std::remove(db_path.c_str());

    return finish_test("scan_detail_route_test", all_ok);
}
