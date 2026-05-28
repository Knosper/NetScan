#include "http/api_profiles.hpp"
#include "http/http_status.hpp"
#include "http/responses.hpp"
#include "http/route_utils.hpp"
#include "http/scan_response_builders.hpp"
#include "service/profile_service.hpp"

#include <nlohmann/json.hpp>

struct ProfileRequestPayload
{
    std::string name;
    std::string target;
    std::string ports;
    bool        host_discovery_only = false;
};

static bool map_profile_name_error(ProfileWriteError error, httplib::Response& res)
{
    switch (error)
    {
    case ProfileWriteError::NameEmpty:
        set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request",
                                      "field 'name' must contain at least one visible character"});
        return true;
    case ProfileWriteError::NameNullByte:
        set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request",
                                      "field 'name' must not contain null bytes"});
        return true;
    case ProfileWriteError::NameControlChar:
        set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request",
                                      "field 'name' must not contain control characters"});
        return true;
    case ProfileWriteError::NameTooLong:
        set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request",
                                      "field 'name' must be at most " +
                                          std::to_string(MAX_PROFILE_NAME_LENGTH) +
                                          " characters"});
        return true;
    case ProfileWriteError::NameConflict:
        set_json_error(res, JsonError{http_status::CONFLICT, "conflict",
                                      "profile name already exists"});
        return true;
    default:
        return false;
    }
}

static nlohmann::json profile_to_json(const ScanProfile& profile)
{
    nlohmann::json payload;
    payload["id"]               = profile.id;
    payload["name"]             = profile.name;
    payload["target"]           = profile.target;
    payload["ports"]            = profile.ports;
    payload["hostDiscoveryOnly"] = profile.host_discovery_only;
    payload["createdAt"]        = profile.created_at;
    return payload;
}

static bool parse_profile_request(const httplib::Request& req, ProfileRequestPayload& payload,
                                  std::string& error)
{
    nlohmann::json body = nlohmann::json::parse(req.body, nullptr, false);
    if (!ensure_json_object(body, error))
        return false;

    nlohmann::json::const_iterator name_it = body.find("name");
    if (name_it == body.end() || !name_it->is_string())
    {
        error = "field 'name' must be a string";
        return false;
    }
    payload.name = name_it->get<std::string>();

    if (!extract_target_field(body, payload.target, error))
        return false;

    if (!extract_requested_ports_field(body, payload.ports, error))
        return false;

    if (!extract_host_discovery_field(body, payload.host_discovery_only, error))
        return false;

    return true;
}

static bool parse_profile_request_or_respond(const httplib::Request& req, httplib::Response& res,
                                             ProfileRequestPayload& payload)
{
    std::string error;
    if (parse_profile_request(req, payload, error))
        return true;

    set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request", error});
    return false;
}

static void handle_profiles_list(httplib::Response& res, ProfileService& service)
{
    std::vector<ScanProfile> profiles = service.list_profiles();
    nlohmann::json body;
    body["profiles"] = nlohmann::json::array();
    for (const ScanProfile& p : profiles)
        body["profiles"].push_back(profile_to_json(p));
    set_json_response(res, http_status::OK, body.dump());
}

static void handle_profile_create(const httplib::Request& req, httplib::Response& res,
                                  ProfileService& service)
{
    if (reject_if_body_too_large(req, res))
        return;

    ProfileRequestPayload payload;
    if (!parse_profile_request_or_respond(req, res, payload))
        return;

    ProfileWriteResult created = service.create_profile(payload.name, payload.target, payload.ports,
                                                        payload.host_discovery_only);
    if (map_profile_name_error(created.error, res))
        return;
    if (!created.profile)
    {
        set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request",
                                      "invalid profile data"});
        return;
    }

    nlohmann::json body;
    body["profile"] = profile_to_json(*created.profile);
    set_json_response(res, http_status::CREATED, body.dump());
}

static void handle_profile_delete(const httplib::Request& req, httplib::Response& res,
                                  ProfileService& service)
{
    int profile_id = 0;
    if (!try_parse_path_id(req, 1U, profile_id))
    {
        set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request",
                                      "invalid profile id"});
        return;
    }

    bool deleted = service.delete_profile(profile_id);
    if (!deleted)
    {
        set_json_error(res, JsonError{http_status::NOT_FOUND, "not_found", "profile not found"});
        return;
    }

    res.status = http_status::NO_CONTENT;
}

static void handle_profile_update(const httplib::Request& req, httplib::Response& res,
                                  ProfileService& service)
{
    int profile_id = 0;
    if (!try_parse_path_id(req, 1U, profile_id))
    {
        set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request",
                                      "invalid profile id"});
        return;
    }
    if (reject_if_body_too_large(req, res))
        return;

    ProfileRequestPayload payload;
    if (!parse_profile_request_or_respond(req, res, payload))
        return;

    ProfileWriteResult updated = service.update_profile(profile_id, payload.name, payload.target,
                                                        payload.ports,
                                                        payload.host_discovery_only);
    if (map_profile_name_error(updated.error, res))
        return;
    if (!updated.profile)
    {
        set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request",
                                      "invalid profile data or profile not found"});
        return;
    }

    nlohmann::json body;
    body["profile"] = profile_to_json(*updated.profile);
    set_json_response(res, http_status::OK, body.dump());
}

static void handle_profile_run(const httplib::Request& req, httplib::Response& res,
                               ProfileService& service)
{
    int profile_id = 0;
    if (!try_parse_path_id(req, 1U, profile_id))
    {
        set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request",
                                      "invalid profile id"});
        return;
    }

    ScanService::StartAsyncResult result = service.run_profile(profile_id);
    if (apply_start_async_common_error(result, res))
        return;
    if (result.status == ScanService::StartAsyncStatus::ValidationError)
    {
        set_json_error(res, JsonError{http_status::NOT_FOUND, "not_found",
                                      result.error_message.empty() ? "profile not found"
                                                                    : result.error_message});
        return;
    }
    nlohmann::json body;
    body["status"] = "queued";
    body["scan"]   = {{"id", result.scan_id}, {"state", "queued"}};
    set_json_response(res, http_status::OK, body.dump());
}

void register_profile_routes(httplib::Server& svr, ProfileService& service)
{
    ProfileService* svc = &service;
    svr.Get("/api/profiles",
            [svc](const httplib::Request&, httplib::Response& res)
            { handle_profiles_list(res, *svc); });
    svr.Post("/api/profiles",
             [svc](const httplib::Request& req, httplib::Response& res)
             { handle_profile_create(req, res, *svc); });
    svr.Delete(R"(/api/profiles/(\d+))",
               [svc](const httplib::Request& req, httplib::Response& res)
               { handle_profile_delete(req, res, *svc); });
    svr.Put(R"(/api/profiles/(\d+))",
            [svc](const httplib::Request& req, httplib::Response& res)
            { handle_profile_update(req, res, *svc); });
    svr.Post(R"(/api/profiles/(\d+)/run)",
             [svc](const httplib::Request& req, httplib::Response& res)
             { handle_profile_run(req, res, *svc); });
}
