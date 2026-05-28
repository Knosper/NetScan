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
#include <memory>
#include <string>
#include <thread>

namespace
{
} // namespace

int main()
{
    bool all_ok = true;

#ifdef _WIN32
    return finish_test("scan_abort_route_test", all_ok);
#else
    ScopedFakeNmap fake_nmap("/tmp/netscan-fake-nmap-route-XXXXXX", "fake route nmap started");
    all_ok = expect(fake_nmap.ready(), "fake nmap route environment should be created") && all_ok;

    const std::string db_path = make_temp_db_path("/tmp/netscan-scan-abort-route-test-XXXXXX.db");
    all_ok = expect(!db_path.empty(), "temporary database path should be created") && all_ok;

    if (!all_ok)
        return finish_test("scan_abort_route_test", false);

    Database db(db_path);
    Logger logger;
    configure_database_runtime(db, logger);
    init_schema(db, logger);

    const TestRepoPaths repo_paths = make_test_repo_paths(__FILE__);

    AppConfig config = make_default_config();
    config.web_dir = repo_paths.web_dir;

    AppContext ctx{config, logger};
    DashboardService dashboard_service(db);
    HealthService health_service(db, logger, "127.0.0.1");
    HostService host_service(db);
    NoteService note_service(db, logger);
    PresenceService presence_service(db, logger);
    ScanService scan_service(db, logger);
    ProfileService profile_service(db, scan_service);
    SchedulerService scheduler_service(db, scan_service, logger);
    SettingsService settings_service(db, repo_paths.conf_path, logger);
    ScanDiffService scan_diff_service(db);
    ScanRepository scan_repo(db);
    ScanHistoryService scan_history_service(db, scan_repo);
    TopologyService topology_service(db, scan_repo);
    ApiServices services{dashboard_service, health_service, host_service, note_service,
                         presence_service, profile_service, scan_diff_service,
                         scan_history_service, scan_service,
                         scheduler_service, settings_service, topology_service};

    httplib::Server server;
    const AuthConfig auth_config;
    register_routes(server, ctx, auth_config, services);
    int port = server.bind_to_any_port("127.0.0.1");
    all_ok = expect(port > 0, "abort route test server should bind to an ephemeral port") && all_ok;

    if (port > 0)
    {
        std::thread server_thread([&server]() { server.listen_after_bind(); });
        httplib::Client client("127.0.0.1", port);
        configure_test_client(client, 10);

        httplib::Result start_result =
            client.Post("/api/scan/start",
                        "{\"target\":\"127.0.0.1\",\"port\":80,\"host_discovery_only\":false}",
                        "application/json");
        all_ok = expect_response_status(start_result, 202, "POST /api/scan/start for abort test") &&
                 all_ok;

        int scan_id = -1;
        if (start_result)
        {
            const nlohmann::json body = nlohmann::json::parse(start_result->body);
            scan_id = body["scan"].value("id", -1);
        }

        all_ok = expect(scan_id > 0, "pre-running abort test scan should have a valid id") && all_ok;

        {
            httplib::Result abort_result =
                client.Post(("/api/scan/" + std::to_string(scan_id) + "/abort").c_str(),
                            httplib::Headers(), "", "");
            all_ok = expect_response_status(
                         abort_result, 200, "POST /api/scan/:id/abort immediately after accept") &&
                     all_ok;
            if (abort_result)
            {
                const nlohmann::json body = nlohmann::json::parse(abort_result->body);
                all_ok = expect(body.value("status", std::string()) == "ok",
                                "immediate abort route success should return status ok") &&
                         all_ok;
                all_ok = expect(body.value("scanId", -1) == scan_id,
                                "immediate abort route should echo the scan id") &&
                         all_ok;
            }
        }

        all_ok =
            expect(wait_for_scan_state(scan_service, scan_id, PersistedScanState::Aborted, 5000),
                   "immediate abort route test scan should persist the aborted state") &&
            all_ok;

        httplib::Result running_start_result =
            client.Post("/api/scan/start",
                        "{\"target\":\"127.0.0.1\",\"port\":81,\"host_discovery_only\":false}",
                        "application/json");
        all_ok =
            expect_response_status(running_start_result, 202,
                                   "POST /api/scan/start for running abort test") &&
            all_ok;

        int running_scan_id = -1;
        if (running_start_result)
        {
            const nlohmann::json body = nlohmann::json::parse(running_start_result->body);
            running_scan_id = body["scan"].value("id", -1);
        }

        all_ok = expect(running_scan_id > 0, "running abort test scan should have a valid id") &&
                 all_ok;
        all_ok =
            expect(wait_for_scan_state(scan_service, running_scan_id, PersistedScanState::Running,
                                       5000),
                   "abort route test scan should reach running state") &&
            all_ok;

        {
            httplib::Result abort_result =
                client.Post(("/api/scan/" + std::to_string(running_scan_id) + "/abort").c_str(), "",
                            "application/json");
            all_ok = expect_response_status(abort_result, 200,
                                            "POST /api/scan/:id/abort for running scan") &&
                     all_ok;
            if (abort_result)
            {
                const nlohmann::json body = nlohmann::json::parse(abort_result->body);
                all_ok = expect(body.value("status", std::string()) == "ok",
                                "abort route success should return status ok") &&
                         all_ok;
                all_ok = expect(body.value("scanId", -1) == running_scan_id,
                                "abort route success should echo the scan id") &&
                         all_ok;
            }
        }

        {
            std::unique_ptr<PersistedScanSummary> aborted_scan =
                scan_service.get_scan(running_scan_id);
            all_ok =
                expect(aborted_scan != nullptr, "aborted route scan should be readable") && all_ok;
            if (aborted_scan)
            {
                all_ok = expect(aborted_scan->state == PersistedScanState::Aborted,
                                "abort route should persist the aborted state") &&
                         all_ok;
            }
        }

        {
            httplib::Result detail_result =
                client.Get(("/api/scans/" + std::to_string(running_scan_id)).c_str());
            all_ok = expect_response_status(detail_result, 200, "GET /api/scans/:id after abort") &&
                     all_ok;
            if (detail_result)
            {
                const nlohmann::json body = nlohmann::json::parse(detail_result->body);
                all_ok = expect(body.value("state", std::string()) == "aborted",
                                "scan detail should expose the aborted state") &&
                         all_ok;
            }
        }

        {
            httplib::Result second_abort =
                client.Post(("/api/scan/" + std::to_string(running_scan_id) + "/abort").c_str(), "",
                            "application/json");
            all_ok = expect_response_status(second_abort, 409,
                                            "POST /api/scan/:id/abort for finished scan") &&
                     all_ok;
        }

        {
            httplib::Result missing_abort =
                client.Post("/api/scan/9999/abort", "", "application/json");
            all_ok = expect_response_status(missing_abort, 404,
                                            "POST /api/scan/:id/abort for missing scan") &&
                     all_ok;
        }

        server.stop();
        server_thread.join();
    }

    std::remove(db_path.c_str());
    return finish_test("scan_abort_route_test", all_ok);
#endif
}
