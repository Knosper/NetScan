#include "http/api_settings.hpp"
#include "http/api_handler.hpp"
#include "http/http_status.hpp"
#include "http/responses.hpp"
#include "http/route_utils.hpp"
#include "app/setup_bootstrap.hpp"
#include "app/shutdown_controller.hpp"
#include "scan/target_validation.hpp"

#include <nlohmann/json.hpp>
#include <vector>

struct ParsedSettingsRequest
{
    SettingsService::Settings settings;
    std::string  error;
    bool         ok = false;
};

struct SettingsRouteContext
{
    SettingsService& service;
    Logger&          logger;
};

static void add_warnings_field(nlohmann::json& body, const std::vector<std::string>& warnings)
{
    if (warnings.empty())
        return;

    nlohmann::json warnings_json = nlohmann::json::array();
    for (const std::string& warning : warnings)
        warnings_json.push_back(warning);
    body["warnings"] = std::move(warnings_json);
}

static const nlohmann::json* validate_settings_envelope(const nlohmann::json& body,
                                                         std::string& error)
{
    const bool has_envelope = body.find("settings") != body.end();
    for (nlohmann::json::const_iterator it = body.begin(); it != body.end(); ++it)
    {
        if (it.key() != "settings")
        {
            error = "unknown top-level field '" + it.key() + "'";
            if (!has_envelope)
                error += "; body must be wrapped: {\"settings\":{...}}";
            return nullptr;
        }
    }

    nlohmann::json::const_iterator settings_it = body.find("settings");
    if (settings_it == body.end() || !settings_it->is_object())
    {
        error = "field 'settings' must be an object";
        return nullptr;
    }
    return &(*settings_it);
}

static bool validate_known_settings_keys(const nlohmann::json& settings, std::string& error)
{
    for (nlohmann::json::const_iterator it = settings.begin(); it != settings.end(); ++it)
    {
        const std::string key = it.key();
        if (key != "log_level" && key != "scan_cooldown_seconds" && key != "user_allowed_targets")
        {
            error = "unknown settings field '" + key + "'";
            return false;
        }
    }
    return true;
}

static bool parse_scalar_settings_fields(const nlohmann::json& settings,
                                         SettingsService::Settings& result, std::string& error)
{
    if (!read_optional_string_field(settings, "log_level", result.log_level, error))
        return false;
    return read_optional_int_field(settings, "scan_cooldown_seconds",
                                   result.scan_cooldown_seconds, error);
}

static bool parse_user_allowed_targets_field(const nlohmann::json& settings,
                                             SettingsService::Settings& result, std::string& error)
{
    nlohmann::json::const_iterator targets_it = settings.find("user_allowed_targets");
    if (targets_it == settings.end())
        return true;

    if (!targets_it->is_array())
    {
        error = "field 'user_allowed_targets' must be an array";
        return false;
    }
    if (targets_it->size() > SettingsService::kMaxUserAllowedTargets)
    {
        error = "field 'user_allowed_targets' must contain at most " +
                std::to_string(SettingsService::kMaxUserAllowedTargets) + " entries";
        return false;
    }

    std::vector<std::string> targets;
    targets.reserve(targets_it->size());
    std::size_t index = 0U;
    for (nlohmann::json::const_iterator it = targets_it->begin(); it != targets_it->end(); ++it)
    {
        if (!it->is_string())
        {
            error = "field 'user_allowed_targets' entries must be strings";
            return false;
        }
        const std::string raw_value = it->get<std::string>();
        if (raw_value.size() > SettingsService::kMaxUserAllowedTargetLength)
        {
            error = "field 'user_allowed_targets[" + std::to_string(index) +
                    "]' must be at most " +
                    std::to_string(SettingsService::kMaxUserAllowedTargetLength) + " characters";
            return false;
        }
        targets.push_back(raw_value);
        ++index;
    }
    result.user_allowed_targets = std::move(targets);
    return true;
}

static ParsedSettingsRequest parse_settings_request(const httplib::Request& req,
                                                    const SettingsService::Settings& current_settings)
{
    ParsedSettingsRequest result;
    nlohmann::json body = nlohmann::json::parse(req.body, nullptr, false);
    if (!ensure_json_object(body, result.error))
        return result;

    const nlohmann::json* settings = validate_settings_envelope(body, result.error);
    if (!settings)
        return result;

    if (!validate_known_settings_keys(*settings, result.error))
        return result;

    result.settings = current_settings;

    if (!parse_scalar_settings_fields(*settings, result.settings, result.error))
        return result;

    if (!parse_user_allowed_targets_field(*settings, result.settings, result.error))
        return result;

    result.ok = true;
    return result;
}

static void handle_settings_get_request(httplib::Response& res, const SettingsRouteContext& ctx)
{
    handle_api_request(
        res, ApiRequestContext{"GET /api/settings", ctx.logger},
        [&ctx]()
        {
            const SettingsService::Settings settings = ctx.service.load_settings();
            nlohmann::json body;
            body["settings"] = {{"log_level", settings.log_level},
                                 {"scan_cooldown_seconds", settings.scan_cooldown_seconds},
                                 {"user_allowed_targets", settings.user_allowed_targets}};
            body["restartRequired"] = false;
            return body.dump();
        });
}

static bool validate_settings_request(const httplib::Request& req, httplib::Response& res)
{
    return validate_json_post_request(req, res);
}

static void handle_settings_save_result(httplib::Response& res,
                                        const SettingsService::SaveResult& result)
{
    switch (result.status)
    {
    case SettingsService::SaveStatus::Saved:
    {
        nlohmann::json body;
        body["status"]          = "ok";
        body["message"]         = "settings saved";
        body["restartRequired"] = false;
        add_warnings_field(body, result.warnings);
        set_json_response(res, http_status::OK, body.dump());
        return;
    }
    case SettingsService::SaveStatus::BadRequest:
        set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request", result.error});
        return;
    case SettingsService::SaveStatus::Error:
    default:
        set_json_error(res, JsonError{http_status::INTERNAL_SERVER_ERROR, "internal_error",
                                      "failed to save settings"});
        return;
    }
}

struct SetupRequestParams
{
    int         port = 8080;
};

static bool parse_setup_request_body(const httplib::Request& req, httplib::Response& res,
                                     nlohmann::json& body)
{
    body = nlohmann::json::parse(req.body, nullptr, false);
    if (body.is_discarded())
    {
        set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request",
                                      "request body must be valid JSON"});
        return false;
    }
    if (!body.is_object())
    {
        set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request",
                                      "request body must be a JSON object"});
        return false;
    }

    return true;
}

static bool validate_setup_request_keys(const nlohmann::json& body, httplib::Response& res)
{
    for (nlohmann::json::const_iterator it = body.begin(); it != body.end(); ++it)
    {
        if (it.key() == "port")
            continue;

        set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request",
                                      "unknown top-level field '" + it.key() + "'"});
        return false;
    }

    return true;
}

static bool parse_setup_request_port(const nlohmann::json& body, httplib::Response& res,
                                     SetupRequestParams& params)
{
    if (!body.contains("port"))
        return true;
    if (!body["port"].is_number_integer())
    {
        set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request",
                                      "field 'port' must be an integer"});
        return false;
    }

    const int port = body["port"].get<int>();
    if (port < 1 || port > 65535)
    {
        set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request",
                                      "field 'port' must be between 1 and 65535"});
        return false;
    }
    params.port = port;
    return true;
}

static bool parse_setup_request(const httplib::Request& req, httplib::Response& res,
                                SetupRequestParams& params)
{
    if (req.body.empty())
        return true;

    nlohmann::json body;
    if (!parse_setup_request_body(req, res, body))
        return false;
    if (!validate_setup_request_keys(body, res))
        return false;
    return parse_setup_request_port(body, res, params);
}

static void handle_settings_setup_request(const httplib::Request& req, httplib::Response& res,
                                          const SettingsRouteContext& ctx)
{
    SetupRequestParams params;
    if (!parse_setup_request(req, res, params))
        return;

    std::string error;
    if (!ctx.service.request_setup(params.port, error))
    {
        set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request", error});
        return;
    }
    nlohmann::json out;
    out["status"]    = "ok";
    out["setup_url"] = "http://127.0.0.1:" + std::to_string(params.port);
    set_json_response(res, http_status::OK, out.dump());
    ShutdownController::request_restart();
}

static void handle_settings_post_request(const httplib::Request& req, httplib::Response& res,
                                         const SettingsRouteContext& ctx)
{
    handle_api_action(
        res, ApiRequestContext{"POST /api/settings", ctx.logger},
        [&req, &res, &ctx]()
        {
            if (!validate_settings_request(req, res))
                return;

            const SettingsService::Settings current_settings = ctx.service.load_settings();
            const ParsedSettingsRequest parsed = parse_settings_request(req, current_settings);
            if (!parsed.ok)
            {
                set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request",
                                              parsed.error});
                return;
            }

            handle_settings_save_result(res, ctx.service.save_settings(parsed.settings));
        });
}

void register_settings_routes(httplib::Server& svr, SettingsService& service, Logger& logger)
{
    const SettingsRouteContext ctx{service, logger};
    svr.Get("/api/settings",
            [ctx](const httplib::Request&, httplib::Response& res)
            { handle_settings_get_request(res, ctx); });
    svr.Post("/api/settings",
             [ctx](const httplib::Request& req, httplib::Response& res)
             { handle_settings_post_request(req, res, ctx); });
    svr.Post("/api/settings/setup",
             [ctx](const httplib::Request& req, httplib::Response& res)
             { handle_settings_setup_request(req, res, ctx); });
}
