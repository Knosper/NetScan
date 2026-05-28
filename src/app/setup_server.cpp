#include "app/application_bootstrap.hpp"
#include "app/http_bootstrap.hpp"
#include "app/setup_server.hpp"
#include "app/setup_bootstrap.hpp"

#include "app/paths.hpp"
#include "app/shutdown_controller.hpp"
#include "http/static_files.hpp"
#include "util/path_utils.hpp"
#include "httplib/httplib.h"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <iostream>
#include <thread>

namespace
{
struct SetupRequest
{
    std::string host;
    int port = 8080;
    bool generate_admin_key = false;
    bool generate_user_key = false;
    std::string error;
    bool ok = false;
};

static SetupRequest parse_setup_request(const std::string& body)
{
    SetupRequest req;
    nlohmann::json j = nlohmann::json::parse(body, nullptr, false);
    if (!j.is_object())
    {
        req.error = "request body must be a JSON object";
        return req;
    }

    if (!j.contains("host"))
    {
        req.error = "field 'host' must be a string";
        return req;
    }
    if (!j.contains("port"))
    {
        req.error = "field 'port' must be an integer";
        return req;
    }
    if (!parse_setup_host_port(j, req.host, req.port, req.error))
        return req;

    if (j.contains("generateAdminKey") && j["generateAdminKey"].is_boolean())
        req.generate_admin_key = j["generateAdminKey"].get<bool>();

    if (j.contains("generateUserKey") && j["generateUserKey"].is_boolean())
        req.generate_user_key = j["generateUserKey"].get<bool>();

    req.ok = true;
    return req;
}

static nlohmann::json run_setup_apply(const SetupRequest& req, const std::string& config_path,
                                      Logger& logger)
{
    SetupPlan plan;
    plan.host = req.host;
    plan.port = req.port;
    plan.generate_admin_key = req.generate_admin_key;
    plan.generate_user_key = req.generate_user_key;

    const SetupResult setup_result = apply_setup_plan(plan, config_path, logger);
    if (!setup_result.ok)
    {
        nlohmann::json err;
        err["error"] = setup_result.error;
        return err;
    }

    nlohmann::json result;
    result["status"] = "ok";
    if (!setup_result.admin_api_key.empty())
        result["adminKey"] = setup_result.admin_api_key;
    if (!setup_result.user_api_key.empty())
        result["userKey"] = setup_result.user_api_key;
    if (!setup_result.admin_api_key.empty() || !setup_result.user_api_key.empty())
        result["warning"] =
            "Save these keys now — they cannot be retrieved later. "
            "Only the salted hash is stored.";
    return result;
}

static void register_setup_info_routes(httplib::Server& svr, const SetupBind& bind)
{
    svr.Get("/api/health",
            [](const httplib::Request&, httplib::Response& res)
            {
                res.set_content(R"({"mode":"setup"})", "application/json");
            });

    svr.Get("/api/setup/defaults",
            [bind](const httplib::Request&, httplib::Response& res)
            {
                nlohmann::json out;
                out["host"] = bind.host;
                out["port"] = bind.port;
                res.set_content(out.dump(), "application/json");
            });
}

static void register_setup_submit_route(httplib::Server& svr, const std::string& config_path,
                                        Logger& logger)
{
    svr.Post("/api/setup",
             [&config_path, &logger](const httplib::Request& req, httplib::Response& res)
             {
                 const SetupRequest setup = parse_setup_request(req.body);
                 if (!setup.ok)
                 {
                     res.status = 400;
                     nlohmann::json err;
                     err["error"] = setup.error;
                     res.set_content(err.dump(), "application/json");
                     return;
                 }

                 const nlohmann::json result = run_setup_apply(setup, config_path, logger);
                 if (result.contains("error"))
                 {
                     res.status = 500;
                     res.set_content(result.dump(), "application/json");
                     return;
                 }

                 res.set_content(result.dump(), "application/json");
                 ShutdownController::request();
             });
}

static void register_setup_reset_route(httplib::Server& svr, const std::string& absolute_config_path)
{
    svr.Post("/api/setup/reset",
             [absolute_config_path](const httplib::Request&, httplib::Response& res)
             {
                 std::remove(absolute_config_path.c_str());
                 res.set_content(R"({"status":"ok"})", "application/json");
                 ShutdownController::request();
             });
}

// web_dir is resolved relative to the executable so it works regardless of CWD.
static void register_setup_static_routes(httplib::Server& svr, const std::string& web_dir)
{
    svr.Get("/app.js",
            [web_dir](const httplib::Request&, httplib::Response& res)
            { serve_static_file(res, join_path(web_dir, "app.js"), "application/javascript"); });
    svr.Get("/style.css",
            [web_dir](const httplib::Request&, httplib::Response& res)
            { serve_static_file(res, join_path(web_dir, "style.css"), "text/css"); });
    svr.Get("/(.*)",
            [web_dir](const httplib::Request&, httplib::Response& res)
            {
                std::string content;
                if (read_file(join_path(web_dir, "index.html"), content))
                    res.set_content(content, "text/html");
                else
                    res.set_content(
                        "NetScan setup server is running.\n"
                        "Web UI assets not found — check that resources/web/ is next to the binary.\n",
                        "text/plain");
            });
}

static int run_setup_server_listen(httplib::Server& svr, const SetupBind& bind,
                                   const std::string& absolute_config_path, Logger& logger)
{
    ShutdownController shutdown;
    if (!shutdown.setup(logger))
        return 1;

    logger.info("Setup mode: open http://" + bind.host + ":" +
                std::to_string(bind.port) + " in your browser to configure NetScan");
    logger.info("Remote setup requires a tunnel or local port-forward, for example: ssh -L " +
                std::to_string(bind.port) + ":127.0.0.1:" + std::to_string(bind.port) +
                " <host>");

    if (!svr.bind_to_port(bind.host.c_str(), bind.port))
    {
        const std::string msg = bind_error_message(bind.host, bind.port);
        logger.error("Setup server: " + msg);
        std::cerr << "Error: Setup server: " << msg << "\n";
        return 1;
    }

    std::thread watcher = shutdown.start_watcher([&svr]() { svr.stop(); }, logger);
    svr.listen_after_bind();
    ShutdownController::request();
    watcher.join();

    // After setup completes, conf.ini is rewritten without setup_pending.
    // Restart if that happened, or if the caller explicitly requested it.
    if (ShutdownController::restart_requested())
        return EXIT_RESTART;
    if (path_exists(absolute_config_path) &&
        !load_persisted_config(absolute_config_path).setup_pending)
        return EXIT_RESTART;
    return 1;
}

} // namespace

int ApplicationBootstrap::run_setup_server(const std::string& config_path, Logger& logger)
{
    httplib::Server svr;
    const std::string absolute_config_path = to_absolute_path(config_path);
    const SetupBind bind = resolve_setup_bind(absolute_config_path);
    const std::string web_dir = join_path(get_executable_dir(), "resources/web");

    register_setup_info_routes(svr, bind);
    register_setup_submit_route(svr, config_path, logger);
    register_setup_reset_route(svr, absolute_config_path);
    register_setup_static_routes(svr, web_dir);

    return run_setup_server_listen(svr, bind, absolute_config_path, logger);
}
