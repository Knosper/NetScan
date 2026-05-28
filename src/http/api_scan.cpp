#include "http/api_scan.hpp"
#include "http/api_handler.hpp"
#include "http/http_status.hpp"
#include "http/responses.hpp"
#include "http/route_utils.hpp"
#include "http/scan_response_builders.hpp"
#include "scan/target_validation.hpp"
#include "scan/scan_types.hpp"
#include "service/scan_history_types.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>

struct ScanRouteContext
{
    ScanService&     service;
    SettingsService& settings_service;
    Logger&          logger;
};

struct DiffAcknowledgementContext
{
    ScanDiffService& service;
    bool             acknowledged = false;
};

static bool parse_scan_id_or_400(const httplib::Request& req, httplib::Response& res,
                                 int& scan_id)
{
    if (try_parse_path_id(req, 1U, scan_id))
        return true;
    set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request", "invalid scan id"});
    return false;
}

static void respond_scan_not_found(httplib::Response& res)
{
    set_json_error(res, JsonError{http_status::NOT_FOUND, "not_found", "scan not found"});
}

static void respond_restricted_target_forbidden(httplib::Response& res)
{
    set_json_error(res, JsonError{http_status::FORBIDDEN, "forbidden",
                                  "target not allowed for restricted key"});
}

static bool validate_port_field(int port_value, const std::string& error_prefix,
                                std::string& error)
{
    if (port_value >= 1 && port_value <= 65535)
        return true;
    error = error_prefix + " must be between 1 and 65535";
    return false;
}

static bool parse_scan_request(const httplib::Request& req, ScanRequest& out, std::string& error)
{
    nlohmann::json body = nlohmann::json::parse(req.body, nullptr, false);
    if (!ensure_json_object(body, error))
        return false;
    if (!extract_target_field(body, out.target, error))
        return false;
    if (!extract_requested_ports_field(body, out.ports, error))
        return false;
    if (!extract_host_discovery_field(body, out.host_discovery_only, error))
        return false;

    if (out.host_discovery_only && !out.ports.empty())
    {
        error = "port specification is not allowed when host discovery only is enabled";
        return false;
    }
    return true;
}

static bool parse_acknowledgement_request(const httplib::Request& req,
                                          ScanDiffAcknowledgementKey& out,
                                          std::string& error)
{
    nlohmann::json body = nlohmann::json::parse(req.body, nullptr, false);
    if (!ensure_json_object(body, error))
        return false;

    nlohmann::json::const_iterator category_it = body.find("category");
    if (category_it == body.end() || !category_it->is_string())
    {
        error = "field 'category' must be a string";
        return false;
    }
    out.category = category_it->get<std::string>();

    nlohmann::json::const_iterator ip_it = body.find("ip");
    if (ip_it == body.end() || !ip_it->is_string())
    {
        error = "field 'ip' must be a string";
        return false;
    }
    out.ip = ip_it->get<std::string>();

    nlohmann::json::const_iterator port_it = body.find("port");
    if (port_it == body.end() || port_it->is_null())
        return true;

    if (!port_it->is_number_integer())
    {
        error = "field 'port' must be an integer";
        return false;
    }

    int port = port_it->get<int>();
    if (!validate_port_field(port, "field 'port'", error))
        return false;

    out.port = port;
    out.has_port = true;
    return true;
}

static bool parse_scan_start_request(const httplib::Request& req, httplib::Response& res,
                                     ScanRequest& request)
{
    std::string parse_error;
    if (parse_scan_request(req, request, parse_error))
        return true;

    set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request", parse_error});
    return false;
}

static bool is_restricted_scan_target_allowed(const httplib::Request& req,
                                              const ScanRequest& request,
                                              const ScanRouteContext& ctx)
{
    if (!req.has_header("X-NetScan-Restricted-Key"))
        return true;

    const std::vector<std::string> allowed_targets = ctx.settings_service.list_allowed_targets();
    return is_user_target_allowed(request.target, allowed_targets);
}

static bool is_restricted_scan_action_allowed(const httplib::Request& req, int scan_id,
                                              const ScanRouteContext& ctx)
{
    if (!req.has_header("X-NetScan-Restricted-Key"))
        return true;

    std::unique_ptr<PersistedScanSummary> scan = ctx.service.get_scan(scan_id);
    if (!scan)
        return true; // 404 handled downstream

    const std::vector<std::string> allowed = ctx.settings_service.list_allowed_targets();
    return is_user_target_allowed(scan->target, allowed);
}

static void handle_scan_start_result(httplib::Response& res,
                                     const ScanService::StartAsyncResult& result)
{
    if (apply_start_async_common_error(result, res))
        return;
    if (result.status == ScanService::StartAsyncStatus::ValidationError)
    {
        set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request",
                                      result.error_message});
        return;
    }

    nlohmann::json body;
    body["status"] = "queued";
    body["scan"]   = {{"id", result.scan_id}, {"state", "queued"}};
    set_json_response(res, http_status::ACCEPTED, body.dump());
}

static void handle_scan_start_request(const httplib::Request& req, httplib::Response& res,
                                      const ScanRouteContext& ctx)
{
    handle_api_action(
        res, ApiRequestContext{"POST /api/scan/start", ctx.logger},
        [&req, &res, &ctx]()
        {
            if (!validate_json_post_request(req, res))
                return;
            ScanRequest request;
            if (!parse_scan_start_request(req, res, request))
                return;
            if (!is_restricted_scan_target_allowed(req, request, ctx))
            {
                respond_restricted_target_forbidden(res);
                return;
            }

            handle_scan_start_result(res, ctx.service.start_async(request));
        });
}

static void handle_scan_status_request(httplib::Response& res, ScanService& service)
{
    ScanService::StatusView status = service.get_status_view();
    set_json_response(res, http_status::OK,
                      scan_status_body(status.scan.get(), status.progress, status.eta_seconds,
                                       status.hosts_found).dump());
}

static bool apply_scan_list_filters(const httplib::Request& req, ScanFilter& filter,
                                    std::string& error)
{
    if (req.has_param("target"))
    {
        const std::string target = req.get_param_value("target");
        if (target.size() > 128)
        {
            error = "query parameter 'target' must not exceed 128 characters";
            return false;
        }
        filter.target = target;
    }
    if (req.has_param("state"))  filter.state  = req.get_param_value("state");
    if (req.has_param("from"))   filter.from   = req.get_param_value("from");
    if (req.has_param("to"))     filter.to     = req.get_param_value("to");

    if (!parse_positive_int_param(req, "limit", 0, filter.limit))
    {
        error = "query parameter 'limit' must be a non-negative integer";
        return false;
    }

    if (!parse_positive_int_param(req, "offset", 0, filter.offset))
    {
        error = "query parameter 'offset' must be a non-negative integer";
        return false;
    }
    return true;
}

static void handle_scans_list_request(const httplib::Request& req, httplib::Response& res,
                                      ScanHistoryService& service,
                                      SettingsService& settings_service)
{
    ScanFilter filter;
    std::string parse_error;
    if (!apply_scan_list_filters(req, filter, parse_error))
    {
        set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request", parse_error});
        return;
    }

    FilteredScansResult result;
    if (req.has_header("X-NetScan-Restricted-Key"))
    {
        result = service.list_filtered_scans(filter, settings_service.list_allowed_targets());
    }
    else
    {
        result = service.list_filtered_scans(filter);
    }

    if (result.status == ListFilteredScansStatus::ValidationError)
    {
        set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request",
                                      result.error_message});
        return;
    }

    set_json_response(res, http_status::OK, scan_list_to_json(result.scans).dump());
}

static void handle_scan_detail_request(const httplib::Request& req, httplib::Response& res,
                                       ScanService& service)
{
    int scan_id = 0;
    if (!parse_scan_id_or_400(req, res, scan_id))
        return;

    std::unique_ptr<PersistedScanSummary> scan = service.get_scan(scan_id);
    if (!scan)
    {
        respond_scan_not_found(res);
        return;
    }

    const auto hosts = service.get_scan_host_summaries(scan_id);
    set_json_response(res, http_status::OK, scan_summary_with_hosts_to_json(*scan, hosts).dump());
}

static void handle_scan_delete_result(httplib::Response& res, int scan_id,
                                      ScanService::DeleteScanStatus result)
{
    switch (result)
    {
    case ScanService::DeleteScanStatus::Deleted:
    {
        nlohmann::json body;
        body["status"]        = "ok";
        body["deletedScanId"] = scan_id;
        set_json_response(res, http_status::OK, body.dump());
        return;
    }
    case ScanService::DeleteScanStatus::NotFound:
        respond_scan_not_found(res);
        return;
    case ScanService::DeleteScanStatus::Conflict:
        set_json_error(res, JsonError{http_status::CONFLICT, "conflict",
                                      "only completed scans can be deleted"});
        return;
    case ScanService::DeleteScanStatus::Error:
    default:
        set_json_error(res, JsonError{http_status::INTERNAL_SERVER_ERROR, "internal_error",
                                      "failed to delete scan"});
        return;
    }
}

static void handle_scan_delete_request(const httplib::Request& req, httplib::Response& res,
                                       const ScanRouteContext& ctx)
{
    int scan_id = 0;
    if (!parse_scan_id_or_400(req, res, scan_id))
        return;
    if (!is_restricted_scan_action_allowed(req, scan_id, ctx))
    {
        respond_restricted_target_forbidden(res);
        return;
    }

    ctx.logger.info("UI requested deletion for scan id " + std::to_string(scan_id));
    handle_scan_delete_result(res, scan_id, ctx.service.delete_scan(scan_id));
}

static void handle_scan_abort_result(httplib::Response& res, int scan_id,
                                     ScanService::TerminateScanStatus result)
{
    switch (result)
    {
    case ScanService::TerminateScanStatus::Aborted:
    {
        nlohmann::json body;
        body["status"] = "ok";
        body["scanId"] = scan_id;
        set_json_response(res, http_status::OK, body.dump());
        return;
    }
    case ScanService::TerminateScanStatus::NotFound:
        respond_scan_not_found(res);
        return;
    case ScanService::TerminateScanStatus::Conflict:
        set_json_error(res, JsonError{http_status::CONFLICT, "conflict", "scan is not running"});
        return;
    case ScanService::TerminateScanStatus::Error:
    default:
        set_json_error(res, JsonError{http_status::INTERNAL_SERVER_ERROR, "internal_error",
                                      "failed to abort scan"});
        return;
    }
}

static void handle_scan_abort_request(const httplib::Request& req, httplib::Response& res,
                                      const ScanRouteContext& ctx)
{
    handle_api_action(
        res, ApiRequestContext{"POST /api/scans/:id/abort", ctx.logger},
        [&req, &res, &ctx]()
        {
            int scan_id = 0;
            if (!parse_scan_id_or_400(req, res, scan_id))
                return;
            if (!is_restricted_scan_action_allowed(req, scan_id, ctx))
            {
                respond_restricted_target_forbidden(res);
                return;
            }

            ctx.logger.info("UI requested abort for scan id " + std::to_string(scan_id));
            handle_scan_abort_result(res, scan_id, ctx.service.terminate_scan(scan_id));
        });
}

static void handle_scan_diff_request(const httplib::Request& req, httplib::Response& res,
                                     ScanDiffService& service)
{
    int scan_id = 0;
    if (!parse_scan_id_or_400(req, res, scan_id))
        return;

    ScanDiffResult result = service.get_scan_diff(scan_id);
    if (result.has_diff())
    {
        auto diff = result.take_diff();
        set_json_response(res, http_status::OK, scan_diff_to_json(*diff).dump());
        return;
    }

    switch (result.error)
    {
    case ScanDiffError::None:
        set_json_error(res, JsonError{http_status::INTERNAL_SERVER_ERROR, "internal_error",
                                      "scan diff result did not contain a diff payload"});
        return;
    case ScanDiffError::NotFound:
        respond_scan_not_found(res);
        return;
    case ScanDiffError::NotDiffable:
        set_json_error(res, JsonError{http_status::CONFLICT, "conflict",
                                      "only completed, non-deleted scans can be diffed"});
        return;
    case ScanDiffError::DatabaseError:
        set_json_error(res, JsonError{http_status::INTERNAL_SERVER_ERROR, "internal_error",
                                      "database error while calculating diff"});
        return;
    }
}

static void handle_acknowledgement_update(const httplib::Request& req, httplib::Response& res,
                                          const DiffAcknowledgementContext& ctx)
{
    ScanDiffAcknowledgementKey key;
    std::string parse_error;
    if (reject_if_body_too_large(req, res))
        return;
    if (!parse_acknowledgement_request(req, key, parse_error))
    {
        set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request", parse_error});
        return;
    }

    ScanDiffAcknowledgementResult result = ctx.acknowledged
        ? ctx.service.acknowledge_diff(key)
        : ctx.service.unacknowledge_diff(key);
    if (result.ok())
    {
        set_json_response(res, http_status::OK, acknowledgement_to_json(result).dump());
        return;
    }

    if (result.error == ScanDiffAcknowledgementError::BadRequest)
    {
        set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request",
                                      result.error_message});
        return;
    }

    set_json_error(res, JsonError{http_status::INTERNAL_SERVER_ERROR, "internal_error",
                                  result.error_message});
}

void register_scan_routes(httplib::Server& svr, const ScanRouteServices& services)
{
    const ScanRouteContext route_ctx{services.scan_service, services.settings_service,
                                     services.logger};
    const DiffAcknowledgementContext ack_ctx{services.diff_service, true};
    const DiffAcknowledgementContext unack_ctx{services.diff_service, false};
    ScanService* const scan_svc = &services.scan_service;
    ScanDiffService* const diff_svc = &services.diff_service;
    ScanHistoryService* const history_svc = &services.history_service;

    svr.Post("/api/scan/start",
             [route_ctx](const httplib::Request& req, httplib::Response& res)
             { handle_scan_start_request(req, res, route_ctx); });
    svr.Get("/api/scan/status",
            [scan_svc](const httplib::Request&, httplib::Response& res)
            { handle_scan_status_request(res, *scan_svc); });
    SettingsService* const settings_svc = &services.settings_service;
    svr.Get("/api/scans",
            [history_svc, settings_svc](const httplib::Request& req, httplib::Response& res)
            { handle_scans_list_request(req, res, *history_svc, *settings_svc); });
    svr.Get(R"(/api/scans/(\d+))",
            [scan_svc](const httplib::Request& req, httplib::Response& res)
            { handle_scan_detail_request(req, res, *scan_svc); });
    svr.Post(R"(/api/scan/(\d+)/abort)",
             [route_ctx](const httplib::Request& req, httplib::Response& res)
             { handle_scan_abort_request(req, res, route_ctx); });
    svr.Get(R"(/api/scans/(\d+)/diff)",
            [diff_svc](const httplib::Request& req, httplib::Response& res)
            { handle_scan_diff_request(req, res, *diff_svc); });
    svr.Put("/api/scan-diff/acknowledgements",
            [ack_ctx](const httplib::Request& req, httplib::Response& res)
            { handle_acknowledgement_update(req, res, ack_ctx); });
    svr.Delete("/api/scan-diff/acknowledgements",
               [unack_ctx](const httplib::Request& req, httplib::Response& res)
               { handle_acknowledgement_update(req, res, unack_ctx); });
    svr.Delete(R"(/api/scans/(\d+))",
               [route_ctx](const httplib::Request& req, httplib::Response& res)
               { handle_scan_delete_request(req, res, route_ctx); });
}
