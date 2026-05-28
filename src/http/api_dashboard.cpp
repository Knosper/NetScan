#include "http/api_dashboard.hpp"
#include "http/api_handler.hpp"
#include "http/http_status.hpp"
#include "http/responses.hpp"

#include <nlohmann/json.hpp>

static nlohmann::json dashboard_stats_to_json(const DashboardData& dashboard)
{
    return {{"hosts", dashboard.stats.hosts},
            {"ports", dashboard.stats.ports},
            {"services", dashboard.stats.services},
            {"lastScan", dashboard.stats.last_scan}};
}

static nlohmann::json dashboard_scans_to_json(const DashboardData& dashboard)
{
    nlohmann::json scans = nlohmann::json::array();
    for (std::vector<DashboardScanRow>::const_iterator it = dashboard.scans.begin();
         it != dashboard.scans.end(); ++it)
    {
        scans.push_back({{"id", it->id},
                         {"target", it->target},
                         {"state", it->status},
                         {"hostDiscoveryOnly", it->host_discovery_only},
                         {"createdAt", it->created_at},
                         {"hostCount", it->host_count},
                         {"portCount", it->port_count}});
    }
    return scans;
}

static nlohmann::json dashboard_hosts_to_json(const DashboardData& dashboard)
{
    nlohmann::json hosts = nlohmann::json::array();
    for (std::vector<DashboardHostRow>::const_iterator it = dashboard.hosts.begin();
         it != dashboard.hosts.end(); ++it)
    {
        hosts.push_back({{"id", it->id}, {"ip", it->ip}, {"name", it->name}});
    }
    return hosts;
}

static nlohmann::json dashboard_ports_to_json(const DashboardData& dashboard)
{
    nlohmann::json ports = nlohmann::json::array();
    for (std::vector<DashboardPortRow>::const_iterator it = dashboard.ports.begin();
         it != dashboard.ports.end(); ++it)
    {
        ports.push_back(
            {{"id", it->id}, {"host", it->host}, {"port", it->port}, {"service", it->service}});
    }
    return ports;
}

static nlohmann::json dashboard_changes_to_json(const DashboardData& dashboard)
{
    return {{"hasBaseline", dashboard.changes.has_baseline},
            {"addedHosts", dashboard.changes.added_hosts},
            {"removedHosts", dashboard.changes.removed_hosts},
            {"addedPorts", dashboard.changes.added_ports},
            {"removedPorts", dashboard.changes.removed_ports},
            {"changedPorts", dashboard.changes.changed_ports},
            {"deltaServices", dashboard.changes.delta_services}};
}

static nlohmann::json dashboard_data_to_json(const DashboardData& dashboard)
{
    nlohmann::json body;
    body["stats"] = dashboard_stats_to_json(dashboard);
    body["scans"] = dashboard_scans_to_json(dashboard);
    if (dashboard.topology_scan_id > 0)
        body["topologyScanId"] = dashboard.topology_scan_id;
    else
        body["topologyScanId"] = nullptr;
    body["hosts"] = dashboard_hosts_to_json(dashboard);
    body["ports"] = dashboard_ports_to_json(dashboard);
    body["changes"] = dashboard_changes_to_json(dashboard);

    return body;
}

static void handle_dashboard_request(httplib::Response& res, DashboardService& service,
                                     Logger& logger)
{
    handle_api_request(res, ApiRequestContext{"GET /api/dashboard", logger},
                       [&service]()
                       { return dashboard_data_to_json(service.get_dashboard()).dump(); });
}

void register_dashboard_routes(httplib::Server& svr, DashboardService& service, Logger& logger)
{
    DashboardService* svc = &service;
    Logger*           log = &logger;
    svr.Get("/api/dashboard",
            [svc, log](const httplib::Request&, httplib::Response& res)
            { handle_dashboard_request(res, *svc, *log); });
}
