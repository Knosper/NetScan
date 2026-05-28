#include "app/app_context.hpp"
#include "app/auth_config.hpp"
#include "app/config.hpp"
#include "db/allowed_target_repository.hpp"
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
#include "util/secret_utils.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{
struct AcceptedScanCase
{
    std::string label;
    std::string request_body;
    std::string target;
    std::string requested_ports;
};

struct ErrorScanCase
{
    std::string label;
    std::string request_body;
    std::string expected_message;
};

struct QueuedScanCase
{
    std::string label;
    std::string request_body;
    std::string target;
    std::string requested_ports;
    bool host_discovery_only;
};

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

AuthConfig make_auth_config(const std::string& admin_key, const std::string& restricted_key)
{
    AuthConfig auth_config;
    if (!admin_key.empty())
        auth_config.admin_api_key_hash = hash_secret_key(admin_key);
    if (!restricted_key.empty())
        auth_config.restricted_api_key_hash = hash_secret_key(restricted_key);
    return auth_config;
}

bool expect_scan_metadata(const nlohmann::json& payload, const std::string& label, int& scan_id)
{
    bool ok = true;
    ok = expect(payload.value("status", std::string()) == "queued",
                label + " should return status queued") &&
         ok;
    ok = expect(payload.contains("scan") && payload["scan"].is_object(),
                label + " should include scan metadata") &&
         ok;
    if (!payload.contains("scan") || !payload["scan"].is_object())
        return false;

    scan_id = payload["scan"].value("id", -1);
    ok = expect(scan_id > 0, label + " should include a positive scan id") && ok;
    return ok;
}

bool expect_persisted_scan(ScanService& service, int scan_id, const std::string& label,
                           const std::string& target, const std::string& requested_ports,
                           bool host_discovery_only)
{
    bool all_ok = true;
    std::unique_ptr<PersistedScanSummary> scan = service.get_scan(scan_id);
    all_ok = expect(scan != nullptr, label + " should persist the scan") && all_ok;
    if (!scan)
        return false;

    all_ok = expect(scan->target == target, label + " should keep the requested target") && all_ok;
    all_ok = expect(scan->requested_ports == requested_ports,
                    label + " should keep the requested ports") &&
             all_ok;
    all_ok = expect(scan->host_discovery_only == host_discovery_only,
                    label + " should keep host_discovery_only") &&
             all_ok;
    return all_ok;
}

httplib::Result post_with_retry_on_rate_limit(httplib::Client& client, const char* path,
                                              const std::string& body,
                                              const std::string& content_type)
{
    const int retry_limit = 25;
    for (int attempt = 0; attempt < retry_limit; ++attempt)
    {
        httplib::Result result = client.Post(path, body, content_type);
        if (!result || result->status != 429)
            return result;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    return client.Post(path, body, content_type);
}

httplib::Result post_with_headers_retry_on_rate_limit(httplib::Client& client, const char* path,
                                                      const httplib::Headers& headers,
                                                      const std::string& body,
                                                      const char* content_type)
{
    const int retry_limit = 25;
    for (int attempt = 0; attempt < retry_limit; ++attempt)
    {
        httplib::Result result = client.Post(path, headers, body, content_type);
        if (!result || result->status != 429)
            return result;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    return client.Post(path, headers, body, content_type);
}

httplib::Result get_with_headers_retry_on_rate_limit(httplib::Client& client, const char* path,
                                                     const httplib::Headers& headers)
{
    const int retry_limit = 25;
    for (int attempt = 0; attempt < retry_limit; ++attempt)
    {
        httplib::Result result = client.Get(path, headers);
        if (!result || result->status != 429)
            return result;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    return client.Get(path, headers);
}

bool expect_accepted_scan_request(httplib::Client& client, ScanService& service,
                                  const AcceptedScanCase& test_case)
{
    bool all_ok = true;

    httplib::Result result = post_with_retry_on_rate_limit(
        client, "/api/scan/start", test_case.request_body, "application/json");
    all_ok = expect_response_status(result, 202, test_case.label) && all_ok;
    if (!result)
        return false;

    int scan_id = -1;
    const nlohmann::json payload = nlohmann::json::parse(result->body);
    all_ok = expect_scan_metadata(payload, test_case.label, scan_id) && all_ok;
    if (scan_id <= 0)
        return false;

    all_ok = expect_persisted_scan(service, scan_id, test_case.label, test_case.target,
                                   test_case.requested_ports, false) &&
             all_ok;

    if (!wait_for_terminal_scan(service, scan_id, 5000))
    {
        service.terminate_all_running_scans("scan start route test cleanup");
        all_ok = expect(wait_for_terminal_scan(service, scan_id, 10000),
                        test_case.label + " should eventually reach a terminal state") &&
                 all_ok;
    }

    return all_ok;
}

bool expect_queued_scan_request(httplib::Client& client, ScanService& service,
                                const QueuedScanCase& test_case)
{
    bool all_ok = true;

    httplib::Result result = post_with_retry_on_rate_limit(
        client, "/api/scan/start", test_case.request_body, "application/json");
    all_ok = expect_response_status(result, 202, test_case.label) && all_ok;
    if (!result)
        return false;

    int scan_id = -1;
    const nlohmann::json payload = nlohmann::json::parse(result->body);
    all_ok = expect_scan_metadata(payload, test_case.label, scan_id) && all_ok;
    if (scan_id <= 0)
        return false;

    all_ok = expect_persisted_scan(service, scan_id, test_case.label, test_case.target,
                                   test_case.requested_ports, test_case.host_discovery_only) &&
             all_ok;

    service.terminate_all_running_scans("scan start route test cleanup");
    return all_ok;
}

bool expect_error_scan_request(httplib::Client& client, const ErrorScanCase& test_case,
                               int expected_status)
{
    bool all_ok = true;

    httplib::Result result = post_with_retry_on_rate_limit(
        client, "/api/scan/start", test_case.request_body, "application/json");
    all_ok = expect_response_status(result, expected_status, test_case.label) && all_ok;
    if (!result)
        return false;

    const nlohmann::json payload = nlohmann::json::parse(result->body);
    all_ok = expect_api_error_body(payload, "bad_request", test_case.expected_message) && all_ok;
    return all_ok;
}

bool expect_unsupported_media_type(httplib::Client& client, const std::string& path,
                                   const std::string& body, const std::string& label)
{
    bool all_ok = true;
    httplib::Result result = post_with_headers_retry_on_rate_limit(
        client, path.c_str(), httplib::Headers(), body, "");
    all_ok = expect_response_status(result, 415, label) && all_ok;
    if (!result)
        return false;

    const nlohmann::json payload = nlohmann::json::parse(result->body);
    all_ok = expect_api_error_body(payload, "unsupported_media_type",
                                   "Content-Type must be application/json") &&
             all_ok;
    return all_ok;
}

bool expect_restricted_scan_list_is_scoped_before_pagination(httplib::Client& client,
                                                             const httplib::Headers& headers)
{
    bool all_ok = true;

    httplib::Result result = get_with_headers_retry_on_rate_limit(
        client, "/api/scans?target=0.0.10&limit=1&offset=1", headers);
    all_ok = expect_response_status(
                 result, 200,
                 "GET /api/scans with restricted key should paginate after allowlist scoping") &&
             all_ok;
    if (!result)
        return false;

    const nlohmann::json body = nlohmann::json::parse(result->body);
    all_ok = expect(body.contains("scans") && body["scans"].is_array(),
                    "restricted scan list should return a scans array") &&
             all_ok;
    if (!body.contains("scans") || !body["scans"].is_array())
        return false;

    all_ok = expect(body["scans"].size() == 1,
                    "restricted scan list should return the second allowed scan for limit=1 offset=1") &&
             all_ok;
    if (body["scans"].size() == 1)
    {
        all_ok = expect(body["scans"][0].value("target", std::string()) == "127.0.0.10",
                        "restricted scan list should skip disallowed scans before offset/limit") &&
                 all_ok;
    }

    return all_ok;
}

bool expect_profile_create_accepts_port_alias(httplib::Client& client)
{
    bool all_ok = true;

    httplib::Result result = post_with_retry_on_rate_limit(
        client, "/api/profiles",
        "{\"name\":\"Port Alias Profile\",\"target\":\"127.0.0.1\",\"port\":443}",
        "application/json");
    all_ok = expect_response_status(result, 201, "POST /api/profiles with port alias") && all_ok;
    if (!result)
        return false;

    const nlohmann::json body = nlohmann::json::parse(result->body);
    all_ok = expect(body.contains("profile") && body["profile"].is_object(),
                    "profile create should return a profile object") &&
             all_ok;
    if (body.contains("profile") && body["profile"].is_object())
    {
        all_ok = expect(body["profile"].value("ports", std::string()) == "443",
                        "profile create should normalize port alias into ports") &&
                 all_ok;
    }

    return all_ok;
}

bool expect_scheduler_create_accepts_port_alias(httplib::Client& client)
{
    bool all_ok = true;

    httplib::Result result = post_with_retry_on_rate_limit(
        client, "/api/scheduler/jobs",
        "{\"target\":\"127.0.0.1\",\"port\":8443,\"interval_seconds\":60}",
        "application/json");
    all_ok = expect_response_status(result, 201, "POST /api/scheduler/jobs with port alias") &&
             all_ok;
    if (!result)
        return false;

    const nlohmann::json body = nlohmann::json::parse(result->body);
    all_ok = expect(body.contains("job") && body["job"].is_object(),
                    "scheduler create should return a job object") &&
             all_ok;
    if (body.contains("job") && body["job"].is_object())
    {
        all_ok = expect(body["job"].value("ports", std::string()) == "8443",
                        "scheduler create should normalize port alias into ports") &&
                 all_ok;
    }

    return all_ok;
}

#ifndef _WIN32
bool expect_conflict_while_scan_running(httplib::Client& client, ScanService& service)
{
    bool all_ok = true;
    ScopedFakeNmap fake_nmap;
    all_ok = expect(fake_nmap.ready(), "fake nmap environment should be created") && all_ok;
    if (!all_ok)
        return false;

    httplib::Result first_result = post_with_retry_on_rate_limit(
        client, "/api/scan/start",
        "{\"target\":\"127.0.0.1\",\"ports\":\"80\",\"host_discovery_only\":false}",
        "application/json");
    all_ok = expect_response_status(first_result, 202,
                                    "first POST /api/scan/start should be accepted") &&
             all_ok;
    if (!first_result)
        return false;

    int first_scan_id = -1;
    const nlohmann::json first_payload = nlohmann::json::parse(first_result->body);
    all_ok = expect_scan_metadata(first_payload, "first POST /api/scan/start", first_scan_id) &&
             all_ok;
    if (first_scan_id <= 0)
        return false;

    httplib::Result second_result = post_with_retry_on_rate_limit(
        client, "/api/scan/start",
        "{\"target\":\"127.0.0.1\",\"ports\":\"81\",\"host_discovery_only\":false}",
        "application/json");
    all_ok = expect_response_status(second_result, 409,
                                    "second POST /api/scan/start should return conflict") &&
             all_ok;
    if (second_result)
    {
        const nlohmann::json second_payload = nlohmann::json::parse(second_result->body);
        all_ok = expect_api_error_body(second_payload, "conflict", "a scan is already running") &&
                 all_ok;
    }

    service.terminate_all_running_scans("scan start route test cleanup");
    all_ok = expect(wait_for_terminal_scan(service, first_scan_id, 10000),
                    "conflict test should clean up the active scan") &&
             all_ok;
    return all_ok;
}
int count_scan_rows(Database& db, ScanRepository& repo)
{
    return db.read([&](sqlite3* h) { return static_cast<int>(repo.list_all_scans(h).size()); });
}

// NOTE: This test mutates the process-global PATH environment variable.
// Must run serially — concurrent tests in the same process would lose nmap
// during the window between setenv and the restore at the end of this function.
// `make test` runs tests sequentially, so this is safe today.
bool expect_dependency_missing_without_scan_row(httplib::Client& client, Database& db,
                                                ScanRepository& repo)
{
    bool all_ok = true;
    std::vector<char> path_template;
    const char raw_template[] = "/tmp/netscan-empty-path-XXXXXX";
    path_template.assign(raw_template, raw_template + sizeof(raw_template));
    char* empty_dir = mkdtemp(path_template.data());
    all_ok = expect(empty_dir != nullptr, "empty PATH directory should be created") && all_ok;
    if (empty_dir == nullptr)
        return false;

    const std::string original_path = std::getenv("PATH") ? std::getenv("PATH") : "";
    const int before_count = count_scan_rows(db, repo);
    setenv("PATH", empty_dir, 1);

    httplib::Result result = post_with_retry_on_rate_limit(
        client, "/api/scan/start",
        "{\"target\":\"127.0.0.1\",\"ports\":\"80\",\"host_discovery_only\":false}",
        "application/json");

    if (original_path.empty())
        unsetenv("PATH");
    else
        setenv("PATH", original_path.c_str(), 1);
    rmdir(empty_dir);

    all_ok = expect_response_status(result, 503, "missing nmap should return 503") && all_ok;
    if (result)
    {
        const nlohmann::json payload = nlohmann::json::parse(result->body);
        all_ok = expect_api_error_body(payload, "dependency_missing", "nmap not found in PATH") &&
                 all_ok;
    }
    all_ok = expect(count_scan_rows(db, repo) == before_count,
                    "missing nmap should not create a scan row") &&
             all_ok;
    return all_ok;
}
#endif
} // namespace

int main()
{
    bool all_ok = true;

    const std::string db_path = make_temp_db_path("/tmp/netscan-scan-start-route-test-XXXXXX.db");
    all_ok = expect(!db_path.empty(), "temporary database path should be created") && all_ok;

    if (!db_path.empty())
    {
        Database db(db_path);
        Logger logger;
        configure_database_runtime(db, logger);
        init_schema(db, logger);
        AllowedTargetRepository allowed_target_repo(db);
        const bool seeded_allowed_targets = db.write(
            [&allowed_target_repo](sqlite3* h)
            {
                return allowed_target_repo.replace_all(h, {"127.0.0.0/8"});
            });
        all_ok = expect(seeded_allowed_targets,
                        "scan start test should seed restricted allowed targets in the database") &&
                 all_ok;

        const TestRepoPaths repo_paths = make_test_repo_paths(__FILE__);

        AppConfig config = make_default_config();
        config.web_dir = repo_paths.web_dir;
        AppContext ctx{config, logger};
        const AuthConfig auth_config = make_auth_config("admin", "user-test-key");
        DashboardService dashboard_service(db);
        HealthService health_service(db, logger, "127.0.0.1");
        HostService host_service(db);
        NoteService note_service(db, logger);
        PresenceService presence_service(db, logger);
        ScanService scan_service(db, logger);
        ProfileService profile_service(db, scan_service);
        ScanDiffService diff_service(db);
        ScanRepository scan_repo(db);
        const int restricted_allowed_older_scan_id = persist_scan(
            db, scan_repo,
            {{"127.0.0.10", "22", false, ""}, PersistedScanState::Completed, false,
             lsm::scan::ScanSnapshot{{{"127.0.0.10", "allowed-older",
                                       {{"tcp", 22, "open", "ssh"}}}}}});
        all_ok = expect(restricted_allowed_older_scan_id > 0,
                        "scan start route test should seed an older allowed scan") &&
                 all_ok;
        const int restricted_blocked_scan_id = persist_scan(
            db, scan_repo,
            {{"10.0.0.10", "80", false, ""}, PersistedScanState::Completed, false,
             lsm::scan::ScanSnapshot{{{"10.0.0.10", "blocked-middle",
                                       {{"tcp", 80, "open", "http"}}}}}});
        all_ok = expect(restricted_blocked_scan_id > 0,
                        "scan start route test should seed a blocked scan") &&
                 all_ok;
        const int restricted_allowed_newer_scan_id = persist_scan(
            db, scan_repo,
            {{"127.10.0.10", "443", false, ""}, PersistedScanState::Completed, false,
             lsm::scan::ScanSnapshot{{{"127.10.0.10", "allowed-newer",
                                       {{"tcp", 443, "open", "https"}}}}}});
        all_ok = expect(restricted_allowed_newer_scan_id > 0,
                        "scan start route test should seed a newer allowed scan") &&
                 all_ok;
        ScanHistoryService history_service(db, scan_repo);
        TopologyService topology_service(db, scan_repo);
        SchedulerService scheduler_service(db, scan_service, logger);
        SettingsService settings_service(db, repo_paths.conf_path, logger);
        ApiServices services{dashboard_service, health_service,   host_service,
                             note_service,      presence_service, profile_service,
                             diff_service,      history_service,  scan_service,
                             scheduler_service, settings_service, topology_service};

        httplib::Server server;
        register_routes(server, ctx, auth_config, services);
        int port = server.bind_to_any_port("127.0.0.1");
        all_ok =
            expect(port > 0, "scan start test server should bind to an ephemeral port") && all_ok;

        if (port > 0)
        {
            std::thread server_thread([&server]() { server.listen_after_bind(); });
            httplib::Client client("127.0.0.1", port);
            configure_test_client(client);
            client.set_default_headers({{"X-API-Key", "admin"}});

            {
                ScopedFakeNmap fake_nmap;
                all_ok = expect(fake_nmap.ready(), "fake nmap environment should be created") &&
                         all_ok;
                scan_service.set_nmap_path(fake_nmap.executable_path());

                const std::vector<AcceptedScanCase> accepted_cases = {
                    {"POST /api/scan/start with IPv4 target",
                     "{\"target\":\"127.0.0.1\",\"ports\":\"80\",\"host_discovery_only\":false}",
                     "127.0.0.1", "80"},
                    {"POST /api/scan/start with IPv6 target",
                     "{\"target\":\"::1\",\"ports\":\"443\",\"host_discovery_only\":false}", "::1",
                     "443"},
                    {"POST /api/scan/start with localhost target",
                     "{\"target\":\"localhost\",\"ports\":\"8080\",\"host_discovery_only\":false}",
                     "localhost", "8080"},
                    {"POST /api/scan/start with IPv4 CIDR target",
                     "{\"target\":\"127.0.0.1/32\",\"ports\":\"80\",\"host_discovery_only\":false}",
                     "127.0.0.1/32", "80"},
                    {"POST /api/scan/start with IPv6 CIDR target",
                     "{\"target\":\"::1/128\",\"ports\":\"443\",\"host_discovery_only\":false}",
                     "::1/128", "443"},
                    {"POST /api/scan/start with port-scan mode and omitted port triggers default scan",
                     "{\"target\":\"127.0.0.1\",\"host_discovery_only\":false}", "127.0.0.1", ""},
                    {"POST /api/scan/start with leading/trailing whitespace target is trimmed",
                     "{\"target\":\"  127.0.0.1  \",\"ports\":\"80\"}", "127.0.0.1", "80"},
                };
                for (std::vector<AcceptedScanCase>::const_iterator it = accepted_cases.begin();
                     it != accepted_cases.end(); ++it)
                {
                    all_ok = expect_accepted_scan_request(client, scan_service, *it) && all_ok;
                }

                const std::vector<ErrorScanCase> invalid_target_cases = {
                    {"POST /api/scan/start with empty target", "{\"target\":\"\"}",
                     "field 'target' must not be empty"},
                    {"POST /api/scan/start with host:port target", "{\"target\":\"127.0.0.1:80\"}",
                     "invalid target format"},
                    {"POST /api/scan/start with bracketed IPv6 target", "{\"target\":\"[::1]:443\"}",
                     "invalid target format"},
                    {"POST /api/scan/start with leading-zero IPv4 target",
                     "{\"target\":\"01.02.03.004\"}", "invalid target format"},
                    {"POST /api/scan/start with oversized IPv4 octet digits",
                     "{\"target\":\"0000.0.0.0\"}", "invalid target format"},
                    {"POST /api/scan/start with zero-padded IPv4 prefix",
                     "{\"target\":\"127.0.0.1/000\"}", "invalid target format"},
                    {"POST /api/scan/start with zero-padded IPv6 prefix", "{\"target\":\"::1/064\"}",
                     "invalid target format"},
                    {"POST /api/scan/start with invalid target select", "{\"target\":\"select\"}",
                     "invalid target format"},
                    {"POST /api/scan/start with invalid target update", "{\"target\":\"update\"}",
                     "invalid target format"},
                    {"POST /api/scan/start with invalid target delete", "{\"target\":\"delete\"}",
                     "invalid target format"},
                    {"POST /api/scan/start with invalid target or", "{\"target\":\"or\"}",
                     "invalid target format"},
                    {"POST /api/scan/start with invalid target handler", "{\"target\":\"handler\"}",
                     "invalid target format"},
                    {"POST /api/scan/start with invalid target scanme.nmap.org",
                     "{\"target\":\"scanme.nmap.org\"}", "invalid target format"},
                    {"POST /api/scan/start with invalid target printer.local",
                     "{\"target\":\"printer.local\"}", "invalid target format"},
                    {"POST /api/scan/start with invalid target host-01", "{\"target\":\"host-01\"}",
                     "invalid target format"},
                    {"POST /api/scan/start with invalid target LOCALHOST", "{\"target\":\"LOCALHOST\"}",
                     "invalid target format"},
                    {"POST /api/scan/start with invalid target LocalHost", "{\"target\":\"LocalHost\"}",
                     "invalid target format"},
                    {"POST /api/scan/start with invalid target 8.8.8.8", "{\"target\":\"8.8.8.8\"}",
                     "invalid target format"},
                    {"POST /api/scan/start with invalid target 0.0.0.0/0", "{\"target\":\"0.0.0.0/0\"}",
                     "invalid target format"},
                    {"POST /api/scan/start with invalid target 2001:db8::1",
                     "{\"target\":\"2001:db8::1\"}", "invalid target format"}};
                for (std::vector<ErrorScanCase>::const_iterator it = invalid_target_cases.begin();
                     it != invalid_target_cases.end(); ++it)
                {
                    all_ok = expect_error_scan_request(client, *it, 400) && all_ok;
                }

                const std::vector<ErrorScanCase> invalid_port_cases = {
                    {"POST /api/scan/start with port zero",
                     "{\"target\":\"127.0.0.1\",\"ports\":\"0\"}", "invalid port token: '0'"},
                    {"POST /api/scan/start with oversized port",
                     "{\"target\":\"127.0.0.1\",\"ports\":\"70000\"}", "invalid port token: '70000'"},
                    {"POST /api/scan/start with host-discovery and explicit port",
                     "{\"target\":\"127.0.0.1\",\"ports\":\"80\",\"host_discovery_only\":true}",
                     "port specification is not allowed when host discovery only is enabled"}};
                httplib::Client invalid_port_client("127.0.0.1", port);
                configure_test_client(invalid_port_client);
                invalid_port_client.set_default_headers({{"X-API-Key", "admin"}});
                for (std::vector<ErrorScanCase>::const_iterator it = invalid_port_cases.begin();
                     it != invalid_port_cases.end(); ++it)
                {
                    all_ok = expect_error_scan_request(invalid_port_client, *it, 400) && all_ok;
                }

                httplib::Client unsupported_client("127.0.0.1", port);
                configure_test_client(unsupported_client);
                unsupported_client.set_default_headers({{"X-API-Key", "admin"}});
                all_ok = expect_unsupported_media_type(
                             unsupported_client, "/api/scan/start",
                             "{\"target\":\"127.0.0.1\",\"ports\":\"80\",\"host_discovery_only\":false}",
                             "POST /api/scan/start without content type") &&
                         all_ok;
                all_ok = expect_profile_create_accepts_port_alias(client) && all_ok;
                all_ok = expect_scheduler_create_accepts_port_alias(client) && all_ok;

                httplib::Client scan_client("127.0.0.1", port);
                configure_test_client(scan_client);

                {
                    const httplib::Headers user_headers = {{"X-NetScan-Restricted-Key", "user-test-key"}};

                    httplib::Result allowed =
                        get_with_headers_retry_on_rate_limit(scan_client, "/api/health", user_headers);
                    all_ok = expect_response_status(allowed, 200, "GET /api/health with user key") &&
                             all_ok;

                    httplib::Result settings_allowed =
                        get_with_headers_retry_on_rate_limit(scan_client, "/api/settings", user_headers);
                    all_ok = expect_response_status(settings_allowed, 200,
                                                    "GET /api/settings with user key should be allowed") &&
                             all_ok;
                    all_ok = expect_restricted_scan_list_is_scoped_before_pagination(
                                 scan_client, user_headers) &&
                             all_ok;

                    httplib::Result allowed_start = post_with_headers_retry_on_rate_limit(
                        scan_client, "/api/scan/start", user_headers,
                        "{\"target\":\"127.0.0.1\",\"ports\":\"80\"}", "application/json");
                    all_ok = expect_response_status(allowed_start, 202,
                                                    "POST /api/scan/start with allowed user target") &&
                             all_ok;
                    if (allowed_start)
                    {
                        const nlohmann::json payload = nlohmann::json::parse(allowed_start->body);
                        int scan_id = -1;
                        all_ok = expect_scan_metadata(payload, "user allowed scan start", scan_id) && all_ok;
                        if (scan_id > 0)
                        {
                            const std::string abort_path =
                                "/api/scan/" + std::to_string(scan_id) + "/abort";
                            httplib::Result abort_result = post_with_headers_retry_on_rate_limit(
                                scan_client, abort_path.c_str(), user_headers, "", "application/json");
                            const bool abort_reachable =
                                abort_result &&
                                (abort_result->status == 200 || abort_result->status == 409);
                            all_ok = expect(abort_reachable,
                                            "POST /api/scan/:id/abort with user key should be reachable (200 or 409)") &&
                                     all_ok;
                        }
                        scan_service.terminate_all_running_scans("scan start route test user cleanup");
                    }

                    httplib::Result blocked_target = post_with_headers_retry_on_rate_limit(
                        scan_client, "/api/scan/start", user_headers,
                        "{\"target\":\"8.8.8.8\",\"ports\":\"80\"}", "application/json");
                    all_ok = expect_response_status(blocked_target, 403,
                                                    "POST /api/scan/start with disallowed user target") &&
                             all_ok;

                    httplib::Result blocked_broader_cidr = post_with_headers_retry_on_rate_limit(
                        scan_client, "/api/scan/start", user_headers,
                        "{\"target\":\"127.0.0.0/4\",\"ports\":\"80\"}", "application/json");
                    all_ok = expect_response_status(blocked_broader_cidr, 403,
                                                    "POST /api/scan/start with broader cidr than allowed") &&
                             all_ok;

                    httplib::Client hostname_client("127.0.0.1", port);
                    configure_test_client(hostname_client);
                    httplib::Result blocked_hostname = post_with_headers_retry_on_rate_limit(
                        hostname_client, "/api/scan/start", user_headers,
                        "{\"target\":\"localhost\",\"ports\":\"80\"}", "application/json");
                    all_ok = expect_response_status(blocked_hostname, 403,
                                                    "POST /api/scan/start with hostname user target") &&
                             all_ok;

                    const httplib::Headers wrong_user_headers = {{"X-NetScan-Restricted-Key", "wrong-key"}};
                    httplib::Result wrong_key =
                        get_with_headers_retry_on_rate_limit(scan_client, "/api/health", wrong_user_headers);
                    all_ok = expect_response_status(wrong_key, 401,
                                                    "GET /api/health with wrong user key should be 401") &&
                             all_ok;
                }

                {
                    const httplib::Headers conflicting_headers = {
                        {"X-API-Key", "admin"},
                        {"X-NetScan-Restricted-Key", "user-test-key"},
                    };
                    httplib::Result conflict =
                        get_with_headers_retry_on_rate_limit(scan_client, "/api/health", conflicting_headers);
                    all_ok = expect_response_status(conflict, 400,
                                                    "request with both auth headers should fail") &&
                             all_ok;
                }

                const std::vector<QueuedScanCase> queued_cases = {
                    {"POST /api/scan/start with host-discovery IPv4 CIDR target",
                     "{\"target\":\"127.0.0.1/32\",\"host_discovery_only\":true}", "127.0.0.1/32", "",
                     true},
                    {"POST /api/scan/start with host-discovery IPv6 CIDR target",
	                     "{\"target\":\"::1/128\",\"host_discovery_only\":true}", "::1/128", "", true}};
                httplib::Client queued_client("127.0.0.1", port);
                configure_test_client(queued_client);
                queued_client.set_default_headers({{"X-API-Key", "admin"}});
                for (std::vector<QueuedScanCase>::const_iterator it = queued_cases.begin();
                     it != queued_cases.end(); ++it)
                {
                    all_ok = expect_queued_scan_request(queued_client, scan_service, *it) && all_ok;
                }

            }
            scan_service.set_nmap_path("");

#ifndef _WIN32
            all_ok = expect_conflict_while_scan_running(client, scan_service) && all_ok;
            all_ok = expect_dependency_missing_without_scan_row(client, db, scan_repo) && all_ok;
#endif

            server.stop();
            server_thread.join();
        }

        {
            AppConfig dual_key_config = make_default_config();
            dual_key_config.web_dir = repo_paths.web_dir;
            AppContext dual_key_ctx{dual_key_config, logger};
            const AuthConfig dual_key_auth_config = make_auth_config("admin", "user-test-key");
            httplib::Server dual_key_server;
            register_routes(dual_key_server, dual_key_ctx, dual_key_auth_config, services);
            int dual_key_port = dual_key_server.bind_to_any_port("127.0.0.1");
            all_ok = expect(dual_key_port > 0,
                            "dual-key auth test server should bind to an ephemeral port") &&
                     all_ok;

            if (dual_key_port > 0)
            {
                std::thread dual_key_server_thread(
                    [&dual_key_server]() { dual_key_server.listen_after_bind(); });
                httplib::Client dual_key_client("127.0.0.1", dual_key_port);
                configure_test_client(dual_key_client);

                const httplib::Headers user_headers = {{"X-NetScan-Restricted-Key", "user-test-key"}};
                httplib::Result user_allowed =
                    get_with_headers_retry_on_rate_limit(dual_key_client, "/api/health",
                                                         user_headers);
                all_ok = expect_response_status(
                             user_allowed, 200,
                             "GET /api/health with user key should succeed when admin key is configured") &&
                         all_ok;

                const httplib::Headers wrong_user_headers = {{"X-NetScan-Restricted-Key", "wrong-key"}};
                httplib::Result user_wrong =
                    get_with_headers_retry_on_rate_limit(dual_key_client, "/api/health",
                                                         wrong_user_headers);
                all_ok = expect_response_status(user_wrong, 401,
                                                "GET /api/health with wrong user key should be 401 in dual-key mode") &&
                         all_ok;

                httplib::Result user_settings =
                    get_with_headers_retry_on_rate_limit(dual_key_client, "/api/settings",
                                                         user_headers);
                all_ok = expect_response_status(user_settings, 200,
                                                "GET /api/settings with user key should be allowed in dual-key mode") &&
                         all_ok;

                const httplib::Headers conflicting_headers = {
                    {"X-API-Key", "admin"},
                    {"X-NetScan-Restricted-Key", "user-test-key"},
                };
                httplib::Result conflict =
                    get_with_headers_retry_on_rate_limit(dual_key_client, "/api/health",
                                                         conflicting_headers);
                all_ok = expect_response_status(conflict, 400,
                                                "request with both auth headers should fail in dual-key mode") &&
                         all_ok;

                dual_key_server.stop();
                dual_key_server_thread.join();
            }
        }
    }

    if (!db_path.empty())
        std::remove(db_path.c_str());

    return finish_test("scan_start_route_test", all_ok);
}
