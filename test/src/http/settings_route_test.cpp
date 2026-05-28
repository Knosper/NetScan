#include "app/app_context.hpp"
#include "app/config.hpp"
#include "app/paths.hpp"
#include "app/setup_server.hpp"
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
#include "util/path_utils.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <chrono>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
std::string read_file_or_empty(const std::string& path)
{
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file.is_open())
        return "";

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return content;
}

bool write_nonloopback_open_mode_fixture(const std::string& path)
{
    std::ofstream f(path.c_str(), std::ios::out | std::ios::trunc | std::ios::binary);
    f << "host=0.0.0.0\n"
      << "port=8089\n"
      << "db_path=./netscan.db\n"
      << "web_dir=./resources/web\n"
      << "log_level=info\n"
      << "scan_cooldown_seconds=0\n";
    return f.good();
}

bool write_valid_nonloopback_fixture(const std::string& path)
{
    return write_nonloopback_open_mode_fixture(path);
}

bool expect_standard_api_headers(const httplib::Result& result, const std::string& label)
{
    bool ok = true;
    ok = expect(static_cast<bool>(result), label + " should return a response") && ok;
    if (!result)
        return false;

    ok = expect(result->get_header_value("X-Content-Type-Options") == "nosniff",
                label + " should set X-Content-Type-Options: nosniff") &&
         ok;
    ok = expect(result->get_header_value("X-Frame-Options") == "DENY",
                label + " should set X-Frame-Options: DENY") &&
         ok;
    ok = expect(result->get_header_value("Cache-Control") == "no-store",
                label + " should set Cache-Control: no-store") &&
         ok;
    return ok;
}
} // namespace

int main()
{
    bool all_ok = true;

    const std::string db_path = make_temp_db_path("/tmp/netscan-settings-route-test-XXXXXX.db");
    const std::string conf_path = make_temp_db_path("/tmp/netscan-settings-config-test-XXXXXX.db");

    all_ok = expect(!db_path.empty(), "temporary database path should be created") && all_ok;
    all_ok = expect(!conf_path.empty(), "temporary config path should be created") && all_ok;

    Logger logger;

    {
        const AppSettings defaults = load_raw_settings(conf_path);
        all_ok = expect(defaults.log_level == "info",
                        "missing config should fall back to default settings log level") &&
                 all_ok;

        const PersistedConfig persisted_defaults = load_persisted_config(conf_path);
        all_ok = expect(persisted_defaults.host == "127.0.0.1",
                        "missing config should fall back to default persisted host") &&
                 all_ok;
        all_ok = expect(persisted_defaults.port == 8080,
                        "missing config should fall back to default persisted port") &&
                 all_ok;
        all_ok = expect(persisted_defaults.log_file.empty(),
                        "missing config should default persisted log_file to empty") &&
                 all_ok;

        const AppConfig resolved_defaults = load_config(conf_path, logger);
        const std::string expected_config_dir = dir_of(to_absolute_path(conf_path));
        all_ok = expect(resolved_defaults.db_path == resolve_path(expected_config_dir, "./netscan.db"),
                        "missing config should resolve default db_path next to conf.ini") &&
                 all_ok;
#ifdef _WIN32
        const std::string expected_default_web_dir =
            join_path(get_executable_dir(), "resources/web");
#else
        const std::string expected_default_web_dir =
            resolve_path(expected_config_dir, "./resources/web");
#endif
        all_ok = expect(
                     resolved_defaults.web_dir == expected_default_web_dir,
                     "missing config should resolve default web_dir") &&
                 all_ok;
    }

    {
        AppSettings invalid = make_default_settings();
        invalid.log_level = "trace";
        std::string error;
        all_ok = expect(!validate_settings(invalid, error),
                        "unsupported log level should fail validation") &&
                 all_ok;
        all_ok = expect(error == "field 'log_level' must be one of: debug, info, warn, error",
                        "unsupported log level should return the expected validation message") &&
                 all_ok;
    }

    {
        PersistedConfig persisted = make_default_persisted_config();
        persisted.port = 8089;
        persisted.log_file = "";
        std::string error;
        all_ok = expect(write_persisted_config_atomically(conf_path, persisted, error),
                        "persisted config writer should save a config file atomically") &&
                 all_ok;

        AppSettings saved = make_default_settings();
        saved.log_level = "warn";
        all_ok = expect(write_settings_file_atomically(conf_path, saved, error),
                        "settings writer should save only the live-editable subset") &&
                 all_ok;

        const std::string content = read_file_or_empty(conf_path);
        all_ok = expect(content.find("port=8089\n") != std::string::npos,
                        "settings writer should preserve restart-required persisted values") &&
                 all_ok;
        all_ok = expect(content.find("log_level=warn\n") != std::string::npos,
                        "settings writer should rewrite the persisted log level") &&
                 all_ok;
        all_ok = expect(content.find("user_allowed_targets=") == std::string::npos,
                        "settings writer should no longer serialize user_allowed_targets to conf.ini") &&
                 all_ok;
        all_ok = expect(content.find("# log_file=./netscan.log\n") != std::string::npos,
                        "empty log_file should be serialized as a disabled commented line") &&
                 all_ok;
    }

    {
        all_ok = expect(write_valid_nonloopback_fixture(conf_path),
                        "non-loopback settings fixture should be written") && all_ok;

        AppSettings saved = make_default_settings();
        saved.log_level = "warn";
        std::string error;
        all_ok = expect(write_settings_file_atomically(conf_path, saved, error),
                        "settings writer should keep accepting non-loopback persisted config") &&
                 all_ok;
    }

    {
        all_ok = expect(write_valid_nonloopback_fixture(conf_path),
                        "non-loopback startup fixture should be written") && all_ok;
        bool loaded = false;
        try
        {
            (void)load_config(conf_path, logger);
            loaded = true;
        }
        catch (const std::runtime_error&)
        {
            loaded = false;
        }
        all_ok = expect(loaded,
                        "load_config should no longer reject non-loopback config on auth grounds") &&
                 all_ok;
    }

    {
        PersistedConfig setup_pending = make_default_persisted_config();
        setup_pending.host = "0.0.0.0";
        setup_pending.port = 9091;
        setup_pending.setup_pending = true;
        std::string error;
        all_ok = expect(write_persisted_config_atomically(conf_path, setup_pending, error),
                        "setup-pending config should be written for setup bind test") &&
                 all_ok;

        const SetupBind bind = resolve_setup_bind(conf_path);
        all_ok = expect(bind.host == "127.0.0.1",
                        "resolve_setup_bind should ignore non-loopback persisted host") &&
                 all_ok;
        all_ok = expect(bind.port == 9091,
                        "resolve_setup_bind should preserve persisted setup port") &&
                 all_ok;
    }

    {
        std::ofstream file(conf_path.c_str(), std::ios::out | std::ios::trunc | std::ios::binary);
        file << "host=0.0.0.0\n"
             << "port=70000\n"
             << "db_path=./netscan.db\n"
             << "web_dir=./resources/web\n"
             << "log_level=info\n"
             << "scan_cooldown_seconds=0\n"
             << "setup_pending=true\n";
        all_ok = expect(file.good(),
                        "setup-pending config with invalid port should be written") &&
                 all_ok;

        const SetupBind bind = resolve_setup_bind(conf_path);
        all_ok = expect(bind.host == "127.0.0.1",
                        "resolve_setup_bind should keep loopback host when falling back") &&
                 all_ok;
        all_ok = expect(bind.port == 8080,
                        "resolve_setup_bind should fall back to default port 8080") &&
                 all_ok;
    }

    {
        PersistedConfig persisted = make_default_persisted_config();
        persisted.port = 8089;
        persisted.log_level = "warn";
        std::string error;
        all_ok = expect(write_persisted_config_atomically(conf_path, persisted, error),
                        "valid config should be restored after invalid startup checks") &&
                 all_ok;
    }

    if (!db_path.empty() && !conf_path.empty())
    {
        Database db(db_path);
        configure_database_runtime(db, logger);
        init_schema(db, logger);
        AllowedTargetRepository preflight_allowed_target_repo(db);
        all_ok = expect(db.write(
                            [&preflight_allowed_target_repo](sqlite3* h)
                            {
                                return preflight_allowed_target_repo.clear(h);
                            }),
                        "settings route test should clear any leftover DB allowlist fixtures") &&
                 all_ok;

        AppConfig config = load_config(conf_path, logger);

        AppContext ctx{config, logger};
        DashboardService dashboard_service(db);
        HealthService health_service(db, logger, "127.0.0.1");
        HostService host_service(db);
        NoteService note_service(db, logger);
        PresenceService presence_service(db, logger);
        ScanService scan_service(db, logger);
        ProfileService profile_service(db, scan_service);
        SchedulerService scheduler_service(db, scan_service, logger);
        SettingsService settings_service(db, conf_path, logger);
        AllowedTargetRepository allowed_target_repo(db);
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
        const int port = server.bind_to_any_port("127.0.0.1");
        all_ok = expect(port > 0, "settings test server should bind to an ephemeral port") &&
                 all_ok;

        if (port > 0)
        {
            std::thread server_thread([&server]() { server.listen_after_bind(); });
            httplib::Client client("127.0.0.1", port);
            client.set_connection_timeout(2, 0);
            client.set_read_timeout(2, 0);
            client.set_write_timeout(2, 0);

            {
                httplib::Result result = client.Get("/api/settings");
                all_ok = expect_response_status(result, 200, "GET /api/settings") && all_ok;
                all_ok = expect_standard_api_headers(result, "GET /api/settings") && all_ok;
                if (result)
                {
                    const nlohmann::json body = nlohmann::json::parse(result->body);
                    all_ok = expect(!body.value("restartRequired", true),
                                    "GET /api/settings should not require restart") &&
                             all_ok;
                    all_ok = expect(body["settings"].value("log_level", std::string()) == "warn",
                                    "GET /api/settings should return only the live-editable settings view") &&
                             all_ok;
                    all_ok = expect(body["settings"]["user_allowed_targets"].is_array(),
                                    "GET /api/settings should return user_allowed_targets as an array") &&
                             all_ok;
                    all_ok = expect(body["settings"]["user_allowed_targets"].empty(),
                                    "GET /api/settings should expose an empty DB-backed allowlist when the table is empty") &&
                             all_ok;
                }
            }

            {
                const std::string body =
                    "{\"settings\":{\"log_level\":\"debug\",\"user_allowed_targets\":[\"192.168.1.0/24\"]}}";
                httplib::Result result = client.Post("/api/settings", body, "application/json");
                all_ok = expect_response_status(result, 200, "POST /api/settings with valid body") &&
                         all_ok;
                if (result)
                {
                    const nlohmann::json payload = nlohmann::json::parse(result->body);
                    all_ok = expect(payload.value("status", std::string()) == "ok",
                                    "POST /api/settings should return status ok") &&
                             all_ok;
                    all_ok = expect(!payload.value("restartRequired", true),
                                    "POST /api/settings should not require restart") &&
                             all_ok;
                }

                PersistedConfig expected = make_default_persisted_config();
                expected.port = 8089;
                expected.log_level = "debug";
                const std::string updated_content = read_file_or_empty(conf_path);
                all_ok = expect(updated_content == serialize_persisted_config(expected),
                                "POST /api/settings should rewrite only the persisted settings subset") &&
                         all_ok;
                all_ok = expect(logger.level() == LogLevel::Debug,
                                "POST /api/settings should apply the new log level live") &&
                         all_ok;

                const std::vector<std::string> stored_targets = db.read(
                    [&allowed_target_repo](sqlite3* h)
                    {
                        return allowed_target_repo.list(h);
                    });
                all_ok = expect(stored_targets.size() == 1 &&
                                    stored_targets[0] == "192.168.1.0/24",
                                "POST /api/settings should persist user_allowed_targets in the database") &&
                         all_ok;

                const SettingsService::Settings reloaded_settings = settings_service.load_settings();
                all_ok = expect(reloaded_settings.user_allowed_targets.size() == 1 &&
                                    reloaded_settings.user_allowed_targets[0] == "192.168.1.0/24",
                                "SettingsService should read user_allowed_targets back from the database") &&
                 all_ok;
            }

            {
                const std::string body = "{\"settings\":{\"log_level\":\"trace\"}}";
                httplib::Result result = client.Post("/api/settings", body, "application/json");
                all_ok = expect_response_status(result, 400,
                                                "POST /api/settings with invalid log level") &&
                         all_ok;
                if (result)
                {
                    const nlohmann::json payload = nlohmann::json::parse(result->body);
                    all_ok = expect(payload.value("type", std::string()) == "bad_request",
                                    "invalid settings response should use bad_request") &&
                             all_ok;
                    all_ok = expect(
                                 payload.value("message", std::string()) ==
                                     "field 'log_level' must be one of: debug, info, warn, error",
                                 "invalid settings response should include the validation message") &&
                             all_ok;
                }
            }

            {
                const std::string body =
                    "{\"settings\":{\"user_allowed_targets\":[\"bad-target\"]}}";
                httplib::Result result =
                    client.Post("/api/settings", body, "application/json");
                all_ok = expect_response_status(result, 400,
                                                "POST /api/settings with invalid allowed target") &&
                         all_ok;
                if (result)
                {
                    const nlohmann::json payload = nlohmann::json::parse(result->body);
                    all_ok = expect(payload.value("message", std::string()) ==
                                        "field 'user_allowed_targets[0]' must be an IP or CIDR target",
                                    "invalid allowed target response should include the validation message") &&
                             all_ok;
                }
            }

            {
                const std::string body =
                    "{\"settings\":{\"user_allowed_targets\":[\"192.168.1.256\"]}}";
                httplib::Result result =
                    client.Post("/api/settings", body, "application/json");
                all_ok = expect_response_status(result, 400,
                                                "POST /api/settings with out-of-range IPv4 octet") &&
                         all_ok;
                if (result)
                {
                    const nlohmann::json payload = nlohmann::json::parse(result->body);
                    all_ok = expect(payload.value("message", std::string()) ==
                                        "field 'user_allowed_targets[0]' must be an IP or CIDR target",
                                    "out-of-range IPv4 octet should return the expected validation message") &&
                             all_ok;
                }
            }

            {
                const std::string body =
                    "{\"settings\":{\"user_allowed_targets\":[\"192.168.1.0/33\"]}}";
                httplib::Result result =
                    client.Post("/api/settings", body, "application/json");
                all_ok = expect_response_status(result, 400,
                                                "POST /api/settings with invalid CIDR mask") &&
                         all_ok;
            }

            {
                const std::string body =
                    "{\"settings\":{\"user_allowed_targets\":[\"::1/128\"]}}";
                httplib::Result result =
                    client.Post("/api/settings", body, "application/json");
                all_ok = expect_response_status(result, 400,
                                                "POST /api/settings with IPv6 allowed target") &&
                         all_ok;
                if (result)
                {
                    const nlohmann::json payload = nlohmann::json::parse(result->body);
                    all_ok = expect(payload.value("message", std::string()) ==
                                        "field 'user_allowed_targets[0]' IPv6 entries are not allowed",
                                    "IPv6 allowed target should return the expected validation message") &&
                             all_ok;
                }
            }

            {
                const std::string body =
                    "{\"settings\":{\"user_allowed_targets\":[\"192.168.1.0/24\",\"::1/128\"]}}";
                httplib::Result result =
                    client.Post("/api/settings", body, "application/json");
                all_ok = expect_response_status(result, 400,
                                                "POST /api/settings with mixed IPv4 + IPv6 allowed targets") &&
                         all_ok;
                if (result)
                {
                    const nlohmann::json payload = nlohmann::json::parse(result->body);
                    all_ok = expect(payload.value("message", std::string()) ==
                                        "field 'user_allowed_targets[1]' IPv6 entries are not allowed",
                                    "mixed IPv4 + IPv6 should report the IPv6 entry index") &&
                             all_ok;
                }
            }

            {
                const std::string body =
                    "{\"settings\":{\"user_allowed_targets\":[\"   \"]}}";
                httplib::Result result =
                    client.Post("/api/settings", body, "application/json");
                all_ok = expect_response_status(result, 400,
                                                "POST /api/settings with whitespace allowed target") &&
                         all_ok;
                if (result)
                {
                    const nlohmann::json payload = nlohmann::json::parse(result->body);
                    all_ok = expect(payload.value("message", std::string()) ==
                                        "field 'user_allowed_targets[0]' must not be empty",
                                    "whitespace allowed target should return the expected validation message") &&
                             all_ok;
                }
            }

            {
                const std::string body =
                    "{\"settings\":{\"user_allowed_targets\":[\"192.168.1.1\\nadmin=true\"]}}";
                httplib::Result result =
                    client.Post("/api/settings", body, "application/json");
                all_ok = expect_response_status(result, 400,
                                                "POST /api/settings with newline injection") &&
                         all_ok;
                if (result)
                {
                    const nlohmann::json payload = nlohmann::json::parse(result->body);
                    all_ok = expect(payload.value("message", std::string()) ==
                                        "field 'user_allowed_targets[0]' must be an IP or CIDR target",
                                    "newline injection should return the expected validation message") &&
                             all_ok;
                }
            }

            {
                const std::string body =
                    "{\"settings\":{\"user_allowed_targets\":[\"\\u0000\"]}}";
                httplib::Result result =
                    client.Post("/api/settings", body, "application/json");
                all_ok = expect_response_status(result, 400,
                                                "POST /api/settings with NUL injection") &&
                         all_ok;
                if (result)
                {
                    const nlohmann::json payload = nlohmann::json::parse(result->body);
                    all_ok = expect(payload.value("message", std::string()) ==
                                        "field 'user_allowed_targets[0]' must be an IP or CIDR target",
                                    "NUL injection should return the expected validation message") &&
                             all_ok;
                }
            }

            {
                const std::string body =
                    "{\"settings\":{\"user_allowed_targets\":[\"192.168.1.1=admin\"]}}";
                httplib::Result result =
                    client.Post("/api/settings", body, "application/json");
                all_ok = expect_response_status(result, 400,
                                                "POST /api/settings with equals injection") &&
                         all_ok;
                if (result)
                {
                    const nlohmann::json payload = nlohmann::json::parse(result->body);
                    all_ok = expect(payload.value("message", std::string()) ==
                                        "field 'user_allowed_targets[0]' must be an IP or CIDR target",
                                    "equals injection should return the expected validation message") &&
                             all_ok;
                }
            }

            {
                const std::string body =
                    "{\"settings\":{\"user_allowed_targets\":[\"192.168.1.1,10.0.0.1\"]}}";
                httplib::Result result =
                    client.Post("/api/settings", body, "application/json");
                all_ok = expect_response_status(result, 400,
                                                "POST /api/settings with comma injection") &&
                         all_ok;
                if (result)
                {
                    const nlohmann::json payload = nlohmann::json::parse(result->body);
                    all_ok = expect(payload.value("message", std::string()) ==
                                        "field 'user_allowed_targets[0]' must be an IP or CIDR target",
                                    "comma injection should return the expected validation message") &&
                             all_ok;
                }
            }

            {
                const std::string long_target(65U, 'a');
                const std::string body = std::string("{\"settings\":{\"user_allowed_targets\":[\"") +
                                         long_target + "\"]}}";
                httplib::Result result =
                    client.Post("/api/settings", body, "application/json");
                all_ok = expect_response_status(result, 400,
                                                "POST /api/settings with oversized allowed target") &&
                         all_ok;
                if (result)
                {
                    const nlohmann::json payload = nlohmann::json::parse(result->body);
                    all_ok = expect(payload.value("message", std::string()) ==
                                        "field 'user_allowed_targets[0]' must be at most 64 characters",
                                    "oversized allowed target should return the expected validation message") &&
                             all_ok;
                }
            }

            {
                std::string body = "{\"settings\":{\"user_allowed_targets\":[";
                for (std::size_t i = 0; i < 257U; ++i)
                {
                    if (i > 0)
                        body += ",";
                    body += "\"192.168.1.1\"";
                }
                body += "]}}";
                httplib::Result result =
                    client.Post("/api/settings", body, "application/json");
                all_ok = expect_response_status(result, 400,
                                                "POST /api/settings with too many allowed targets") &&
                         all_ok;
                if (result)
                {
                    const nlohmann::json payload = nlohmann::json::parse(result->body);
                    all_ok = expect(payload.value("message", std::string()) ==
                                        "field 'user_allowed_targets' must contain at most 256 entries",
                                    "too many allowed targets should return the expected cap message") &&
                             all_ok;
                }
            }

            {
                const std::string body =
                    "{\"settings\":{\"user_allowed_targets\":[\"192.168.1.1\",\"192.168.1.1\"]}}";
                httplib::Result result =
                    client.Post("/api/settings", body, "application/json");
                all_ok = expect_response_status(result, 400,
                                                "POST /api/settings with duplicate allowed targets") &&
                         all_ok;
                if (result)
                {
                    const nlohmann::json payload = nlohmann::json::parse(result->body);
                    all_ok = expect(payload.value("message", std::string()) ==
                                        "field 'user_allowed_targets[1]' duplicates an earlier entry",
                                    "duplicate allowed targets should return the expected validation message") &&
                             all_ok;
                }
            }

            {
                const std::string body =
                    "{\"settings\":{\"log_level\":\"info\",\"evil_key\":\"x\"}}";
                httplib::Result result = client.Post("/api/settings", body, "application/json");
                all_ok = expect_response_status(result, 400,
                                                "POST /api/settings with unknown settings key") &&
                         all_ok;
                if (result)
                {
                    const nlohmann::json payload = nlohmann::json::parse(result->body);
                    all_ok = expect(payload.value("type", std::string()) == "bad_request",
                                    "unknown settings key response should use bad_request") &&
                             all_ok;
                    all_ok = expect(payload.value("message", std::string()) ==
                                        "unknown settings field 'evil_key'",
                                    "unknown settings key response should name the unexpected key") &&
                             all_ok;
                }
            }

            {
                const std::string body = "{\"log_level\":\"debug\"}";
                httplib::Result result = client.Post("/api/settings", body, "application/json");
                all_ok = expect_response_status(result, 400,
                                                "POST /api/settings with flat body (missing envelope)") &&
                         all_ok;
                if (result)
                {
                    const nlohmann::json payload = nlohmann::json::parse(result->body);
                    all_ok = expect(payload.value("message", std::string()) ==
                                        "unknown top-level field 'log_level'; body must be wrapped: {\"settings\":{...}}",
                                    "flat body response should hint at the required envelope") &&
                             all_ok;
                }
            }

            {
                const std::string body = "{\"settings\":{\"log_level\":\"info\"}}";
                httplib::Result result =
                    client.Post("/api/settings", httplib::Headers(), body, "");
                all_ok = expect_response_status(result, 415,
                                                "POST /api/settings without content type") &&
                         all_ok;
                all_ok = expect_standard_api_headers(result,
                                                     "POST /api/settings without content type") &&
                         all_ok;
                if (result)
                {
                    const nlohmann::json payload = nlohmann::json::parse(result->body);
                    all_ok = expect(payload.value("type", std::string()) ==
                                        "unsupported_media_type",
                                    "missing content type response should use unsupported_media_type") &&
                             all_ok;
                    all_ok = expect(payload.value("message", std::string()) ==
                                        "Content-Type must be application/json",
                                    "missing content type response should explain the required content type") &&
                             all_ok;
                }
            }

            {
                const std::string oversized_padding(70000U, 'a');
                const std::string body = std::string("{\"settings\":{\"log_level\":\"info\"},\"padding\":\"") +
                                         oversized_padding + "\"}";
                httplib::Result result = client.Post("/api/settings", body, "application/json");
                all_ok = expect_response_status(result, 413,
                                                "POST /api/settings with oversized body") &&
                         all_ok;
            }

            std::this_thread::sleep_for(std::chrono::seconds(3));

            {
                httplib::Result result = client.Post("/api/settings/setup", "", "application/json");
                all_ok = expect_response_status(result, 200,
                                                "POST /api/settings/setup without body") &&
                         all_ok;
                all_ok = expect_standard_api_headers(result,
                                                     "POST /api/settings/setup without body") &&
                         all_ok;
                if (result)
                {
                    const nlohmann::json payload = nlohmann::json::parse(result->body);
                    all_ok = expect(payload.value("status", std::string()) == "ok",
                                    "POST /api/settings/setup should return status ok") &&
                             all_ok;
                    all_ok = expect(payload.value("setup_url", std::string()) ==
                                        "http://127.0.0.1:8080",
                                    "POST /api/settings/setup without body should use loopback default URL") &&
                             all_ok;
                }

                const PersistedConfig pending = load_persisted_config(conf_path);
                all_ok = expect(pending.setup_pending,
                                "POST /api/settings/setup should mark config as setup-pending") &&
                         all_ok;
                all_ok = expect(pending.host == "127.0.0.1",
                                "POST /api/settings/setup should persist default loopback host") &&
                         all_ok;
                all_ok = expect(pending.port == 8080,
                                "POST /api/settings/setup without body should persist default setup port") &&
                         all_ok;
            }

            {
                const std::string body = "{\"port\":8091}";
                httplib::Result result = client.Post("/api/settings/setup", body, "application/json");
                all_ok = expect_response_status(result, 200,
                                                "POST /api/settings/setup with explicit port") &&
                         all_ok;
                if (result)
                {
                    const nlohmann::json payload = nlohmann::json::parse(result->body);
                    all_ok = expect(payload.value("setup_url", std::string()) ==
                                        "http://127.0.0.1:8091",
                                    "POST /api/settings/setup should return loopback URL for explicit port") &&
                             all_ok;
                }

                const PersistedConfig pending = load_persisted_config(conf_path);
                all_ok = expect(pending.setup_pending,
                                "POST /api/settings/setup with explicit port should keep setup pending flag") &&
                         all_ok;
                all_ok = expect(pending.host == "127.0.0.1",
                                "POST /api/settings/setup with explicit port should keep loopback host") &&
                         all_ok;
                all_ok = expect(pending.port == 8091,
                                "POST /api/settings/setup with explicit port should persist the requested port") &&
                         all_ok;
            }

            {
                const std::string body = "{\"host\":\"0.0.0.0\",\"port\":8080}";
                httplib::Result result = client.Post("/api/settings/setup", body, "application/json");
                all_ok = expect_response_status(result, 400,
                                                "POST /api/settings/setup with host override") &&
                         all_ok;
                if (result)
                {
                    const nlohmann::json payload = nlohmann::json::parse(result->body);
                    all_ok = expect(payload.value("message", std::string()) ==
                                        "unknown top-level field 'host'",
                                    "POST /api/settings/setup with host override should reject host field") &&
                             all_ok;
                }
            }

            server.stop();
            server_thread.join();
        }
    }

    if (!db_path.empty())
        std::remove(db_path.c_str());
    if (!conf_path.empty())
        std::remove(conf_path.c_str());
    if (!conf_path.empty())
        std::remove((conf_path + ".tmp").c_str());

    return finish_test("settings_route_test", all_ok);
}
