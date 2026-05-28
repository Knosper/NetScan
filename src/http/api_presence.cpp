#include "http/api_presence.hpp"
#include "http/api_handler.hpp"
#include "http/http_status.hpp"
#include "http/responses.hpp"
#include "http/route_utils.hpp"

#include <nlohmann/json.hpp>

namespace
{
nlohmann::json result_to_json(const PresenceCheckResult& result)
{
    nlohmann::json body;
    body["id"] = result.id;
    body["trackerId"] = result.tracker_id;
    body["status"] = result.status;
    body["latencyMs"] = result.latency_ms < 0 ? nlohmann::json(nullptr)
                                              : nlohmann::json(result.latency_ms);
    body["error"] = result.error;
    body["checkedAt"] = result.checked_at;
    return body;
}

nlohmann::json tracker_to_json(const PresenceTracker& tracker)
{
    nlohmann::json body;
    body["id"] = tracker.id;
    body["target"] = tracker.target;
    body["checkType"] = tracker.check_type;
    body["port"] = tracker.port == 0 ? nlohmann::json(nullptr) : nlohmann::json(tracker.port);
    body["url"] = tracker.url;
    body["intervalSeconds"] = tracker.interval_seconds;
    body["timeoutMs"] = tracker.timeout_ms;
    body["enabled"] = tracker.enabled;
    body["createdAt"] = tracker.created_at;
    body["updatedAt"] = tracker.updated_at;
    body["lastResult"] = tracker.last_checked_at.empty()
                             ? nlohmann::json(nullptr)
                             : result_to_json(PresenceCheckResult{
                                   0, tracker.id, tracker.last_status,
                                   tracker.last_latency_ms, tracker.last_error,
                                   tracker.last_checked_at});
    return body;
}

bool parse_tracker_request(const httplib::Request& req, PresenceTracker& tracker,
                           std::string& error)
{
    nlohmann::json body = nlohmann::json::parse(req.body, nullptr, false);
    if (!ensure_json_object(body, error))
        return false;

    tracker.interval_seconds = presence_limits::DEFAULT_INTERVAL_SECONDS;
    tracker.timeout_ms = presence_limits::DEFAULT_TIMEOUT_MS;
    tracker.enabled = true;

    if (!read_optional_string_field(body, "target", tracker.target, error) ||
        !read_optional_string_field(body, "check_type", tracker.check_type, error) ||
        !read_optional_string_field(body, "url", tracker.url, error) ||
        !read_optional_int_field(body, "port", tracker.port, error) ||
        !read_optional_int_field(body, "interval_seconds", tracker.interval_seconds, error) ||
        !read_optional_int_field(body, "timeout_ms", tracker.timeout_ms, error) ||
        !read_optional_bool_field(body, "enabled", tracker.enabled, error))
        return false;

    if (tracker.check_type.empty())
        tracker.check_type = "ping";
    return true;
}

int status_code_for(PresenceStatus status)
{
    if (status == PresenceStatus::BadRequest)
        return http_status::BAD_REQUEST;
    if (status == PresenceStatus::NotFound)
        return http_status::NOT_FOUND;
    if (status == PresenceStatus::LimitExceeded)
        return http_status::CONFLICT;
    return http_status::INTERNAL_SERVER_ERROR;
}

const char* error_type_for(PresenceStatus status)
{
    if (status == PresenceStatus::NotFound)
        return "not_found";
    if (status == PresenceStatus::LimitExceeded)
        return "limit_exceeded";
    if (status == PresenceStatus::BadRequest)
        return "bad_request";
    return "internal_error";
}

void set_presence_error(httplib::Response& res, PresenceStatus status, const std::string& message)
{
    set_json_error(res, JsonError{status_code_for(status), error_type_for(status), message});
}

void handle_tracker_list(httplib::Response& res, PresenceService& service, Logger& logger)
{
    handle_api_action(res, ApiRequestContext{"GET /api/presence/trackers", logger},
                      [&res, &service]()
                      {
                          nlohmann::json body;
                          body["trackers"] = nlohmann::json::array();
                          for (const PresenceTracker& tracker : service.list_trackers())
                              body["trackers"].push_back(tracker_to_json(tracker));
                          set_json_response(res, http_status::OK, body.dump());
                      });
}

void handle_tracker_get(const httplib::Request& req, httplib::Response& res,
                        PresenceService& service, Logger& logger)
{
    handle_api_action(res, ApiRequestContext{"GET /api/presence/trackers/:id", logger},
                      [&req, &res, &service]()
                      {
                          int id = 0;
                          if (!try_parse_path_id(req, 1U, id))
                          {
                              set_presence_error(res, PresenceStatus::BadRequest, "invalid tracker id");
                              return;
                          }

                          std::unique_ptr<PresenceTracker> tracker = service.get_tracker(id);
                          if (!tracker)
                          {
                              set_presence_error(res, PresenceStatus::NotFound, "tracker not found");
                              return;
                          }

                          nlohmann::json body;
                          body["tracker"] = tracker_to_json(*tracker);
                          set_json_response(res, http_status::OK, body.dump());
                      });
}

void handle_tracker_create(const httplib::Request& req, httplib::Response& res,
                           PresenceService& service, Logger& logger)
{
    handle_api_action(res, ApiRequestContext{"POST /api/presence/trackers", logger},
                      [&req, &res, &service]()
                      {
                          if (reject_if_body_too_large(req, res))
                              return;

                          PresenceTracker tracker;
                          std::string error;
                          if (!parse_tracker_request(req, tracker, error))
                          {
                              set_presence_error(res, PresenceStatus::BadRequest, error);
                              return;
                          }

                          PresenceWriteResult result = service.create_tracker(tracker);
                          if (result.status != PresenceStatus::Ok)
                          {
                              set_presence_error(res, result.status, result.message);
                              return;
                          }

                          nlohmann::json body;
                          body["tracker"] = tracker_to_json(result.tracker);
                          set_json_response(res, http_status::CREATED, body.dump());
                      });
}

void handle_tracker_update(const httplib::Request& req, httplib::Response& res,
                           PresenceService& service, Logger& logger)
{
    handle_api_action(res, ApiRequestContext{"PATCH /api/presence/trackers/:id", logger},
                      [&req, &res, &service]()
                      {
                          int id = 0;
                          if (!try_parse_path_id(req, 1U, id))
                          {
                              set_presence_error(res, PresenceStatus::BadRequest, "invalid tracker id");
                              return;
                          }
                          if (reject_if_body_too_large(req, res))
                              return;

                          PresenceTracker tracker;
                          std::string error;
                          if (!parse_tracker_request(req, tracker, error))
                          {
                              set_presence_error(res, PresenceStatus::BadRequest, error);
                              return;
                          }

                          PresenceWriteResult result = service.update_tracker(id, tracker);
                          if (result.status != PresenceStatus::Ok)
                          {
                              set_presence_error(res, result.status, result.message);
                              return;
                          }

                          nlohmann::json body;
                          body["tracker"] = tracker_to_json(result.tracker);
                          set_json_response(res, http_status::OK, body.dump());
                      });
}

void handle_tracker_delete(const httplib::Request& req, httplib::Response& res,
                           PresenceService& service, Logger& logger)
{
    handle_api_action(res, ApiRequestContext{"DELETE /api/presence/trackers/:id", logger},
                      [&req, &res, &service]()
                      {
                          int id = 0;
                          if (!try_parse_path_id(req, 1U, id))
                          {
                              set_presence_error(res, PresenceStatus::BadRequest, "invalid tracker id");
                              return;
                          }

                          if (!service.delete_tracker(id))
                          {
                              set_presence_error(res, PresenceStatus::NotFound, "tracker not found");
                              return;
                          }
                          res.status = http_status::NO_CONTENT;
                      });
}

void handle_tracker_check(const httplib::Request& req, httplib::Response& res,
                          PresenceService& service, Logger& logger)
{
    handle_api_action(res, ApiRequestContext{"POST /api/presence/trackers/:id/check", logger},
                      [&req, &res, &service]()
                      {
                          int id = 0;
                          if (!try_parse_path_id(req, 1U, id))
                          {
                              set_presence_error(res, PresenceStatus::BadRequest, "invalid tracker id");
                              return;
                          }

                          PresenceRunResult result = service.run_check(id);
                          if (result.status != PresenceStatus::Ok)
                          {
                              set_presence_error(res, result.status, result.message);
                              return;
                          }

                          nlohmann::json body;
                          body["result"] = result_to_json(result.result);
                          set_json_response(res, http_status::OK, body.dump());
                      });
}

void handle_result_list(const httplib::Request& req, httplib::Response& res,
                        PresenceService& service, Logger& logger)
{
    handle_api_action(res, ApiRequestContext{"GET /api/presence/trackers/:id/results", logger},
                      [&req, &res, &service]()
                      {
                          int id = 0;
                          if (!try_parse_path_id(req, 1U, id))
                          {
                              set_presence_error(res, PresenceStatus::BadRequest, "invalid tracker id");
                              return;
                          }

                          if (!service.get_tracker(id))
                          {
                              set_presence_error(res, PresenceStatus::NotFound, "tracker not found");
                              return;
                          }

                          int limit = presence_limits::DEFAULT_RESULT_LIMIT;
                          if (!parse_positive_int_param(req, "limit", presence_limits::DEFAULT_RESULT_LIMIT, limit))
                          {
                              set_presence_error(res, PresenceStatus::BadRequest, "query parameter 'limit' is invalid");
                              return;
                          }

                          nlohmann::json body;
                          body["results"] = nlohmann::json::array();
                          for (const PresenceCheckResult& result : service.list_results(id, limit))
                              body["results"].push_back(result_to_json(result));
                          set_json_response(res, http_status::OK, body.dump());
                      });
}
} // namespace

void register_presence_routes(httplib::Server& svr, PresenceService& service, Logger& logger)
{
    PresenceService* svc = &service;
    Logger*          log = &logger;
    svr.Get("/api/presence/trackers",
            [svc, log](const httplib::Request&, httplib::Response& res)
            { handle_tracker_list(res, *svc, *log); });
    svr.Post("/api/presence/trackers",
             [svc, log](const httplib::Request& req, httplib::Response& res)
             { handle_tracker_create(req, res, *svc, *log); });
    svr.Get(R"(/api/presence/trackers/(\d+))",
            [svc, log](const httplib::Request& req, httplib::Response& res)
            { handle_tracker_get(req, res, *svc, *log); });
    svr.Patch(R"(/api/presence/trackers/(\d+))",
              [svc, log](const httplib::Request& req, httplib::Response& res)
              { handle_tracker_update(req, res, *svc, *log); });
    svr.Delete(R"(/api/presence/trackers/(\d+))",
               [svc, log](const httplib::Request& req, httplib::Response& res)
               { handle_tracker_delete(req, res, *svc, *log); });
    svr.Post(R"(/api/presence/trackers/(\d+)/check)",
             [svc, log](const httplib::Request& req, httplib::Response& res)
             { handle_tracker_check(req, res, *svc, *log); });
    svr.Get(R"(/api/presence/trackers/(\d+)/results)",
            [svc, log](const httplib::Request& req, httplib::Response& res)
            { handle_result_list(req, res, *svc, *log); });
}
