#include "http/api_scheduler.hpp"
#include "http/http_status.hpp"
#include "http/responses.hpp"
#include "http/route_utils.hpp"
#include "scan/port_spec_validator.hpp"
#include "scan/target_validation.hpp"
#include "util/string_utils.hpp"

#include <nlohmann/json.hpp>

static nlohmann::json scheduled_job_to_json(const ScheduledJob& job)
{
    nlohmann::json payload;
    payload["id"]               = job.id;
    payload["target"]           = job.target;
    payload["ports"]            = job.ports;
    payload["hostDiscoveryOnly"] = job.host_discovery_only;
    payload["intervalSeconds"]  = job.interval_seconds;
    payload["enabled"]          = job.enabled;
    payload["lastRunAt"]        = job.last_run_at.empty()
                                      ? nlohmann::json(nullptr)
                                      : nlohmann::json(job.last_run_at);
    payload["nextRunAt"]        = job.next_run_at.empty()
                                      ? nlohmann::json(nullptr)
                                      : nlohmann::json(job.next_run_at);
    return payload;
}

static bool parse_job_target(const nlohmann::json& body, ScheduledJob& out, std::string& error)
{
    nlohmann::json::const_iterator target_it = body.find("target");
    if (target_it == body.end() || !target_it->is_string() ||
        target_it->get<std::string>().empty())
    {
        error = "field 'target' must be a non-empty string";
        return false;
    }

    out.target = util::trim(target_it->get<std::string>());
    if (!is_valid_scan_target(out.target))
    {
        error = "field 'target' contains invalid characters or is not a valid scan target";
        return false;
    }

    return true;
}

static bool parse_job_interval(const nlohmann::json& body, ScheduledJob& out, std::string& error)
{
    nlohmann::json::const_iterator interval_it = body.find("interval_seconds");
    if (interval_it == body.end() || !interval_it->is_number_integer() ||
        interval_it->get<int>() <= 0)
    {
        error = "field 'interval_seconds' must be an integer > 0";
        return false;
    }

    out.interval_seconds = interval_it->get<int>();
    return true;
}

static bool validate_job_ports(ScheduledJob& out, std::string& error)
{
    if (out.ports.empty())
        return true;

    const PortSpecValidation port_validation = validate_port_spec(out.ports);
    if (!port_validation.ok)
    {
        error = "field 'ports': " + port_validation.error_message;
        return false;
    }

    return true;
}

static void parse_job_enabled_flag(const nlohmann::json& body, ScheduledJob& out)
{
    nlohmann::json::const_iterator enabled_it = body.find("enabled");
    if (enabled_it != body.end() && enabled_it->is_boolean())
        out.enabled = enabled_it->get<bool>();
}

static bool parse_job_request(const httplib::Request& req, ScheduledJob& out, std::string& error)
{
    const nlohmann::json body = nlohmann::json::parse(req.body, nullptr, false);
    if (!ensure_json_object(body, error))
        return false;
    if (!parse_job_target(body, out, error))
        return false;
    if (!parse_job_interval(body, out, error))
        return false;

    if (!extract_requested_ports_field(body, out.ports, error))
        return false;
    if (!validate_job_ports(out, error))
        return false;

    if (!extract_host_discovery_field(body, out.host_discovery_only, error))
        return false;

    parse_job_enabled_flag(body, out);
    return true;
}

static void handle_scheduler_jobs_list(httplib::Response& res, SchedulerService& service)
{
    std::vector<ScheduledJob> jobs = service.list_jobs();
    nlohmann::json body;
    body["jobs"] = nlohmann::json::array();
    for (const ScheduledJob& job : jobs)
        body["jobs"].push_back(scheduled_job_to_json(job));
    set_json_response(res, http_status::OK, body.dump());
}

static void handle_scheduler_job_create(const httplib::Request& req, httplib::Response& res,
                                        SchedulerService& service)
{
    if (reject_if_body_too_large(req, res))
        return;

    ScheduledJob job;
    std::string parse_error;
    if (!parse_job_request(req, job, parse_error))
    {
        set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request", parse_error});
        return;
    }

    int id = service.create_job(job);
    if (id <= 0)
    {
        set_json_error(res, JsonError{http_status::INTERNAL_SERVER_ERROR, "internal_error",
                                      "failed to create job"});
        return;
    }

    std::unique_ptr<ScheduledJob> created = service.get_job(id);
    if (!created)
    {
        set_json_error(res, JsonError{http_status::INTERNAL_SERVER_ERROR, "internal_error",
                                      "failed to retrieve created job"});
        return;
    }

    nlohmann::json body;
    body["job"] = scheduled_job_to_json(*created);
    set_json_response(res, http_status::CREATED, body.dump());
}

static void handle_scheduler_job_delete(const httplib::Request& req, httplib::Response& res,
                                        SchedulerService& service)
{
    int job_id = 0;
    if (!try_parse_path_id(req, 1U, job_id))
    {
        set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request", "invalid job id"});
        return;
    }

    bool deleted = service.delete_job(job_id);
    if (!deleted)
    {
        set_json_error(res, JsonError{http_status::NOT_FOUND, "not_found", "job not found"});
        return;
    }

    res.status = http_status::NO_CONTENT;
}

static void handle_scheduler_job_patch(const httplib::Request& req, httplib::Response& res,
                                       SchedulerService& service)
{
    int job_id = 0;
    if (!try_parse_path_id(req, 1U, job_id))
    {
        set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request", "invalid job id"});
        return;
    }
    if (reject_if_body_too_large(req, res))
        return;

    nlohmann::json body = nlohmann::json::parse(req.body, nullptr, false);
    std::string parse_error;
    if (!ensure_json_object(body, parse_error))
    {
        set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request", parse_error});
        return;
    }

    nlohmann::json::const_iterator enabled_it = body.find("enabled");
    if (enabled_it == body.end() || !enabled_it->is_boolean())
    {
        set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request",
                                      "field 'enabled' must be a boolean"});
        return;
    }

    bool ok = service.set_enabled(job_id, enabled_it->get<bool>());
    if (!ok)
    {
        set_json_error(res, JsonError{http_status::NOT_FOUND, "not_found", "job not found"});
        return;
    }

    std::unique_ptr<ScheduledJob> updated = service.get_job(job_id);
    if (!updated)
    {
        set_json_error(res, JsonError{http_status::INTERNAL_SERVER_ERROR, "internal_error",
                                      "failed to retrieve updated job"});
        return;
    }

    nlohmann::json resp;
    resp["job"] = scheduled_job_to_json(*updated);
    set_json_response(res, http_status::OK, resp.dump());
}

void register_scheduler_routes(httplib::Server& svr, SchedulerService& service)
{
    SchedulerService* svc = &service;
    svr.Get("/api/scheduler/jobs",
            [svc](const httplib::Request&, httplib::Response& res)
            { handle_scheduler_jobs_list(res, *svc); });
    svr.Post("/api/scheduler/jobs",
             [svc](const httplib::Request& req, httplib::Response& res)
             { handle_scheduler_job_create(req, res, *svc); });
    svr.Delete(R"(/api/scheduler/jobs/(\d+))",
               [svc](const httplib::Request& req, httplib::Response& res)
               { handle_scheduler_job_delete(req, res, *svc); });
    svr.Patch(R"(/api/scheduler/jobs/(\d+))",
              [svc](const httplib::Request& req, httplib::Response& res)
              { handle_scheduler_job_patch(req, res, *svc); });
}
