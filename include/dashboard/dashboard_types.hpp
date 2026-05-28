#ifndef DASHBOARD_DASHBOARD_TYPES_HPP
#define DASHBOARD_DASHBOARD_TYPES_HPP

#include <string>
#include <vector>

struct DashboardStats
{
    int         hosts = 0;
    int         ports = 0;
    int         services = 0;
    std::string last_scan;
};

struct DashboardScanRow
{
    int         id = 0;
    std::string target;
    std::string status;
    std::string created_at;
    bool        host_discovery_only = false;
    int         host_count = 0;
    int         port_count = 0;
};

struct DashboardHostRow
{
    int         id = 0;
    std::string ip;
    std::string name;
};

struct DashboardPortRow
{
    int         id = 0;
    std::string host;
    int         port = 0;
    std::string service;
};

struct DashboardChangeCard
{
    bool    has_baseline   = false;
    int     added_hosts    = 0;
    int     removed_hosts  = 0;
    int     added_ports    = 0;
    int     removed_ports  = 0;
    int     changed_ports  = 0;
    int     delta_services = 0;
};

struct DashboardData
{
    DashboardStats                  stats;
    DashboardChangeCard             changes;
    std::vector<DashboardScanRow>   scans;
    std::vector<DashboardHostRow>   hosts;
    std::vector<DashboardPortRow>   ports;
    int                             topology_scan_id = 0;
};

#endif
