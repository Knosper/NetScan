#include "http/scan_response_builders.hpp"
#include "db/scan_read_types.hpp"
#include "http/http_status.hpp"
#include "scan/scan_diff_types.hpp"
#include "scan/scan_types.hpp"

#include <nlohmann/json.hpp>
#include <vector>

namespace
{
nlohmann::json host_diff_entry_to_json(const HostDiffEntry& host)
{
    return {{"ip", host.ip},
            {"name", host.name},
            {"acknowledgementKey", host.acknowledgement_key},
            {"acknowledged", host.acknowledged}};
}

nlohmann::json port_diff_entry_to_json(const PortDiffEntry& port)
{
    return {{"ip", port.ip},
            {"port", port.port},
            {"name", port.name},
            {"service", port.service},
            {"acknowledgementKey", port.acknowledgement_key},
            {"acknowledged", port.acknowledged}};
}

nlohmann::json build_diff_host_arrays(const HostDiffResult& host_diff)
{
    nlohmann::json result;
    result["newHosts"] = nlohmann::json::array();
    for (const auto& host : host_diff.new_hosts)
        result["newHosts"].push_back(host_diff_entry_to_json(host));

    result["disappearedHosts"] = nlohmann::json::array();
    for (const auto& host : host_diff.disappeared_hosts)
        result["disappearedHosts"].push_back(host_diff_entry_to_json(host));

    return result;
}

nlohmann::json build_diff_port_arrays(const PortDiffResult& port_diff)
{
    nlohmann::json result;
    result["newOpenPorts"] = nlohmann::json::array();
    for (const auto& port : port_diff.new_open_ports)
        result["newOpenPorts"].push_back(port_diff_entry_to_json(port));

    result["disappearedPorts"] = nlohmann::json::array();
    for (const auto& port : port_diff.disappeared_ports)
        result["disappearedPorts"].push_back(port_diff_entry_to_json(port));

    return result;
}
} // namespace

nlohmann::json scan_summary_to_json(const PersistedScanSummary& scan)
{
    nlohmann::json payload;
    payload["id"]               = scan.id;
    payload["target"]           = scan.target;
    payload["state"]            = to_api_status(scan.state);
    payload["message"]          = scan.message;
    payload["command"]          = scan.command;
    payload["stderrText"]       = scan.stderr_text;
    payload["exitCode"]         = scan.has_exit_code ? nlohmann::json(scan.exit_code)
                                                     : nlohmann::json(nullptr);
    payload["createdAt"]        = scan.created_at;
    payload["startedAt"]        = scan.started_at;
    payload["finishedAt"]       = scan.finished_at;
    payload["deleted"]          = scan.deleted;
    payload["hostDiscoveryOnly"] = scan.host_discovery_only;
    payload["scanType"]         = scan.host_discovery_only ? "host_discovery" : "port_scan";
    payload["portCoverageKnown"] = scan.port_coverage_known;
    payload["requestedPorts"]   = scan.requested_ports.empty()
                                      ? nlohmann::json(nullptr)
                                      : nlohmann::json(scan.requested_ports);
    return payload;
}

nlohmann::json scan_summary_with_hosts_to_json(const PersistedScanSummary& scan,
                                               const std::vector<ScanHostSummary>& hosts)
{
    nlohmann::json payload = scan_summary_to_json(scan);
    payload["hosts"] = nlohmann::json::array();
    for (const auto& h : hosts)
    {
        payload["hosts"].push_back({{"ip", h.ip},
                                    {"hostname", h.hostname},
                                    {"openPortCount", h.open_port_count}});
    }
    return payload;
}

nlohmann::json scan_status_body(const PersistedScanSummary* scan, int progress,
                                int eta_seconds, int hosts_found)
{
    nlohmann::json body;
    if (!scan)
    {
        body["status"] = "idle";
        body["scan"]   = nullptr;
        return body;
    }

    body["status"] = to_api_status(scan->state);
    nlohmann::json scan_json = scan_summary_to_json(*scan);
    const bool is_running = scan->state == PersistedScanState::Running;
    if (is_running && progress >= 0)
        scan_json["progress"] = clamp_progress_percent(progress);
    if (is_running && eta_seconds >= 0)
        scan_json["etaSeconds"] = eta_seconds;
    if (is_running && hosts_found >= 0)
        scan_json["hostsFound"] = hosts_found;
    body["scan"] = std::move(scan_json);
    return body;
}

nlohmann::json scan_list_to_json(const std::vector<PersistedScanSummary>& scans)
{
    nlohmann::json body;
    body["scans"] = nlohmann::json::array();
    for (const auto& scan : scans)
        body["scans"].push_back(scan_summary_to_json(scan));
    return body;
}

nlohmann::json scan_diff_to_json(const ScanDiffModel& diff)
{
    const bool has_baseline = diff.baseline != nullptr;

    nlohmann::json body;
    body["scan"]       = scan_summary_to_json(diff.scan);
    body["baseline"]   = has_baseline ? scan_summary_to_json(*diff.baseline) : nlohmann::json(nullptr);
    body["hasBaseline"] = has_baseline;
    body["comparable"] = true;

    if (!has_baseline)
        body["reason"] = "no_baseline";

    nlohmann::json host_arrays = build_diff_host_arrays(diff.host_diff);
    body["newHosts"]         = std::move(host_arrays["newHosts"]);
    body["disappearedHosts"] = std::move(host_arrays["disappearedHosts"]);

    nlohmann::json port_arrays = build_diff_port_arrays(diff.port_diff);
    body["newOpenPorts"]     = std::move(port_arrays["newOpenPorts"]);
    body["disappearedPorts"] = std::move(port_arrays["disappearedPorts"]);

    return body;
}

nlohmann::json acknowledgement_to_json(const ScanDiffAcknowledgementResult& result)
{
    nlohmann::json key;
    key["category"]         = result.key.category;
    key["ip"]               = result.key.ip;
    key["port"]             = result.key.has_port ? nlohmann::json(result.key.port)
                                                  : nlohmann::json(nullptr);
    key["acknowledgementKey"] = scan_diff_acknowledgement_key_id(result.key);
    key["acknowledged"]     = result.acknowledged;

    nlohmann::json body;
    body["status"]          = "ok";
    body["acknowledgement"] = key;
    return body;
}

bool apply_start_async_common_error(const IScanStarter::StartAsyncResult& result,
                                    httplib::Response& res)
{
    using Status = IScanStarter::StartAsyncStatus;
    switch (result.status)
    {
    case Status::PolicyViolation:
        set_json_error(res, JsonError{http_status::TOO_MANY_REQUESTS, "policy_violation",
                                      result.error_message});
        return true;
    case Status::Conflict:
        set_json_error(res, JsonError{http_status::CONFLICT, "conflict",
                                      "a scan is already running"});
        return true;
    case Status::DependencyMissing:
        set_json_error(res, JsonError{http_status::SERVICE_UNAVAILABLE, "dependency_missing",
                                      result.error_message.empty() ? "nmap not found in PATH"
                                                                    : result.error_message});
        return true;
    case Status::Error:
    default:
        set_json_error(res, JsonError{http_status::INTERNAL_SERVER_ERROR, "internal_error",
                                      "failed to start scan"});
        return true;
    case Status::Accepted:
    case Status::ValidationError:
        return false;
    }
}
