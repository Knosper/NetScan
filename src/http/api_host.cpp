#include "http/api_host.hpp"
#include "http/api_handler.hpp"
#include "http/http_status.hpp"
#include "http/responses.hpp"
#include "http/route_utils.hpp"
#include "scan/scan_types.hpp"
#include "scan/target_validation.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <set>
#include <string>

static std::string tag_value_to_string(const nlohmann::json& tag)
{
    if (tag.is_string())
        return tag.get<std::string>();

    if (!tag.is_object())
        return "";

    const nlohmann::json::const_iterator name_it = tag.find("name");
    if (name_it == tag.end() || !name_it->is_string())
        return "";

    std::string value = name_it->get<std::string>();
    const nlohmann::json::const_iterator color_it = tag.find("color");
    if (color_it != tag.end() && color_it->is_string() && !color_it->get<std::string>().empty())
        value += ":" + color_it->get<std::string>();
    return value;
}

static std::string tags_array_to_string(const nlohmann::json& tags)
{
    std::string result;
    for (const auto& tag : tags)
    {
        const std::string value = tag_value_to_string(tag);
        if (value.empty())
            continue;
        if (!result.empty())
            result += ",";
        result += value;
    }
    return result;
}

static std::string normalize_tags_for_api(const std::string& tags_str)
{
    if (tags_str.empty())
        return "";

    try
    {
        auto parsed = nlohmann::json::parse(tags_str);
        if (parsed.is_array())
            return tags_array_to_string(parsed);
    }
    catch (const nlohmann::json::parse_error&)
    {
        return tags_str;
    }
    return tags_str;
}

static nlohmann::json meta_to_json(const HostMeta& meta)
{
    return {
        {"displayName", meta.display_name}, {"role", meta.role}, {"tags", normalize_tags_for_api(meta.tags)}};
}

static nlohmann::json host_overview_to_json(const HostOverview& host)
{
    nlohmann::json payload;
    payload["id"] = host.id;
    payload["ip"] = host.ip;
    payload["name"] = host.name;
    payload["scanCount"] = host.scan_count;
    payload["lastSeenAt"] = host.last_seen_at;
    payload["lastScanId"] =
        host.last_scan_id > 0 ? nlohmann::json(host.last_scan_id) : nlohmann::json(nullptr);
    payload["openPortCount"] = host.open_port_count;
    payload["meta"] = meta_to_json(host.meta);
    return payload;
}

static nlohmann::json host_detail_to_json(const HostDetail& detail)
{
    nlohmann::json body;
    body["host"] = {{"id", detail.host.id}, {"ip", detail.host.ip}, {"name", detail.host.name}};
    body["meta"] = meta_to_json(detail.meta);
    body["history"] = nlohmann::json::array();

    for (const auto& history_entry : detail.history)
    {
        nlohmann::json entry;
        entry["scan"] = {{"id", history_entry.scan.id},
                         {"state", history_entry.scan.deleted ? "deleted" : to_api_status(history_entry.scan.state)},
                         {"createdAt", history_entry.scan.created_at},
                         {"startedAt", history_entry.scan.started_at},
                         {"finishedAt", history_entry.scan.finished_at},
                         {"deleted", history_entry.scan.deleted}};
        entry["ports"] = nlohmann::json::array();
        for (const auto& port : history_entry.ports)
        {
            entry["ports"].push_back({{"port", port.port},
                                      {"service", port.service},
                                      {"state", port.state}});
        }
        body["history"].push_back(entry);
    }

    return body;
}

static void filter_hosts_by_allowlist(std::vector<HostOverview>& hosts,
                                      const std::vector<std::string>& allowed_targets)
{
    hosts.erase(std::remove_if(hosts.begin(), hosts.end(), [&allowed_targets](const HostOverview& h)
                               { return !is_user_target_allowed(h.ip, allowed_targets); }),
                hosts.end());
}

static bool parse_host_list_params(const httplib::Request& req, httplib::Response& res,
                                    int& limit, int& offset, HostListFilter& filter)
{
    if (!parse_positive_int_param(req, "limit", 500, limit))
    {
        set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request",
                                      "query parameter 'limit' must be an integer >= 0"});
        return false;
    }
    if (!parse_positive_int_param(req, "offset", 0, offset))
    {
        set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request",
                                      "query parameter 'offset' must be an integer >= 0"});
        return false;
    }
    if (req.has_param("q"))
    {
        const std::string q = req.get_param_value("q");
        if (q.size() > 128)
        {
            set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request",
                                          "query parameter 'q' must not exceed 128 characters"});
            return false;
        }
        filter.search = q;
    }
    if (req.has_param("openPortsOnly"))
    {
        const std::string val = req.get_param_value("openPortsOnly");
        filter.open_ports_only = (val == "true" || val == "1" || val == "yes");
    }
    return true;
}

static void handle_hosts_list_request(const httplib::Request& req, httplib::Response& res,
                                      HostService& service, SettingsService& settings_service,
                                      Logger& logger)
{
    handle_api_action(
        res, ApiRequestContext{"GET /api/hosts", logger},
        [&req, &res, &service, &settings_service]()
        {
            int limit = 500;
            int offset = 0;
            HostListFilter filter;
            if (!parse_host_list_params(req, res, limit, offset, filter))
                return;

            std::vector<HostOverview> hosts = service.list_hosts(filter, limit, offset);

            if (req.has_header("X-NetScan-Restricted-Key"))
            {
                const std::vector<std::string> allowed_targets =
                    settings_service.list_allowed_targets();
                filter_hosts_by_allowlist(hosts, allowed_targets);
            }

            const int total = static_cast<int>(hosts.size());
            const int page = (limit > 0) ? (offset / limit) + 1 : 1;

            nlohmann::json body;
            body["hosts"] = nlohmann::json::array();
            body["total"] = total;
            body["page"] = page;
            body["pageSize"] = limit;
            for (const auto& host : hosts)
                body["hosts"].push_back(host_overview_to_json(host));
            set_json_response(res, http_status::OK, body.dump());
        });
}

static void handle_host_detail_request(const httplib::Request& req, httplib::Response& res,
                                       HostService& service, Logger& logger)
{
    handle_api_action(
        res, ApiRequestContext{"GET /api/hosts/:ip", logger},
        [&req, &res, &service]()
        {
            std::string host_ip;
            if (!try_parse_host_ip(req, host_ip))
            {
                set_json_error(
                    res, JsonError{http_status::BAD_REQUEST, "bad_request", "invalid host ip"});
                return;
            }

            std::unique_ptr<HostDetail> detail = service.get_host_detail(host_ip);
            if (!detail)
            {
                set_json_error(res,
                               JsonError{http_status::NOT_FOUND, "not_found", "host not found"});
                return;
            }

            set_json_response(res, http_status::OK, host_detail_to_json(*detail).dump());
        });
}

static void handle_prune_closed_ports_request(httplib::Response& res, HostService& service,
                                              Logger& logger)
{
    handle_api_action(res, ApiRequestContext{"DELETE /api/hosts/ports/closed", logger},
                      [&res, &service]()
                      {
                          const int deleted = service.prune_closed_ports();
                          if (deleted < 0)
                          {
                              set_json_error(res,
                                             JsonError{http_status::INTERNAL_SERVER_ERROR,
                                                       "internal_error", "prune operation failed"});
                              return;
                          }

                          nlohmann::json body;
                          body["status"] = "ok";
                          body["deleted"] = deleted;
                          set_json_response(res, http_status::OK, body.dump());
                      });
}

static nlohmann::json parse_host_meta_body(const httplib::Request& req)
{
    try
    {
        return nlohmann::json::parse(req.body);
    }
    catch (...)
    {
        return nullptr;
    }
}

static void apply_host_meta_patch(const nlohmann::json& body, HostMeta& meta)
{
    if (body.contains("displayName") && body["displayName"].is_string())
        meta.display_name = body["displayName"].get<std::string>();
    if (body.contains("role") && body["role"].is_string())
        meta.role = body["role"].get<std::string>();
    if (!body.contains("tags"))
        return;

    const auto& tags = body["tags"];
    if (tags.is_string())
        meta.tags = tags.get<std::string>();
    else if (tags.is_array())
        meta.tags = tags_array_to_string(tags);
}

static void handle_patch_host_meta(const httplib::Request& req, httplib::Response& res,
                                   HostService& service, Logger& logger)
{
    handle_api_action(
        res, ApiRequestContext{"PATCH /api/hosts/:ip/meta", logger},
        [&req, &res, &service]()
        {
            std::string host_ip;
            if (!try_parse_host_ip(req, host_ip))
            {
                set_json_error(
                    res, JsonError{http_status::BAD_REQUEST, "bad_request", "invalid host ip"});
                return;
            }
            if (reject_if_body_too_large(req, res))
                return;

            const nlohmann::json body = parse_host_meta_body(req);
            std::string err;
            if (!ensure_json_object(body, err))
            {
                set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request", err});
                return;
            }

            HostMeta meta;
            apply_host_meta_patch(body, meta);
            service.update_host_meta(host_ip, meta);
            set_json_response(res, http_status::OK, "{\"status\":\"ok\"}");
        });
}

static const std::set<std::string>& allowed_meta_fields()
{
    static const std::set<std::string> fields = {"display_name", "role", "tags"};
    return fields;
}

static void handle_delete_host_meta_field(const httplib::Request& req, httplib::Response& res,
                                          HostService& service, Logger& logger)
{
    handle_api_action(res, ApiRequestContext{"DELETE /api/hosts/:ip/meta/:field", logger},
                      [&req, &res, &service]()
                      {
                          std::string host_ip;
                          if (!try_parse_host_ip(req, host_ip))
                          {
                              set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request",
                                                            "invalid host ip"});
                              return;
                          }

                          const std::string field =
                              req.matches.size() > 2 ? req.matches[2].str() : "";
                          const auto& allowed = allowed_meta_fields();
                          if (allowed.find(field) == allowed.end())
                          {
                              set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request",
                                                            "unknown meta field"});
                              return;
                          }

                          service.clear_host_meta_field(host_ip, field);
                          set_json_response(res, http_status::OK, "{\"status\":\"ok\"}");
                      });
}

void register_host_routes(httplib::Server& svr, HostService& service,
                          SettingsService& settings_service, Logger& logger)
{
    HostService* svc = &service;
    SettingsService* settings = &settings_service;
    Logger* log = &logger;
    svr.Get("/api/hosts", [svc, settings, log](const httplib::Request& req, httplib::Response& res)
            { handle_hosts_list_request(req, res, *svc, *settings, *log); });
    svr.Delete("/api/hosts/ports/closed",
               [svc, log](const httplib::Request&, httplib::Response& res)
               { handle_prune_closed_ports_request(res, *svc, *log); });
    svr.Patch(R"(/api/hosts/([^/]+)/meta)",
              [svc, log](const httplib::Request& req, httplib::Response& res)
              { handle_patch_host_meta(req, res, *svc, *log); });
    svr.Delete(R"(/api/hosts/([^/]+)/meta/([^/]+))",
               [svc, log](const httplib::Request& req, httplib::Response& res)
               { handle_delete_host_meta_field(req, res, *svc, *log); });
    svr.Get(R"(/api/hosts/([^/]+))", [svc, log](const httplib::Request& req, httplib::Response& res)
            { handle_host_detail_request(req, res, *svc, *log); });
}
