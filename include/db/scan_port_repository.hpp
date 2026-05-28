#ifndef DB_SCAN_PORT_REPOSITORY_HPP
#define DB_SCAN_PORT_REPOSITORY_HPP

#include "db/scan_read_types.hpp"
#include <string>
#include <vector>

struct sqlite3;

struct ScanPortParams
{
    int         scan_id = 0;
    std::string host_ip;
    int         port = 0;
    std::string protocol;
    std::string state;
    std::string service;
};

struct PortRecordParams
{
    int         host_id = 0;
    int         port = 0;
    std::string protocol;
    std::string state;
    std::string service;
    int         last_open_scan_id = 0;
    int         last_observed_scan_id = 0;
};

struct PersistedPortStateRow
{
    int         host_id = 0;
    int         port = 0;
    std::string protocol;
    std::string state;
    std::string service;
    int         last_open_scan_id = 0;
    int         last_observed_scan_id = 0;
};

class ScanPortRepository
{
public:
    bool delete_ports_for_host(sqlite3* h, int host_id);
    int prune_closed_ports(sqlite3* h);
    bool insert_port(sqlite3* h, const PortRecordParams& params);
    bool insert_scan_port(sqlite3* h, const ScanPortParams& params);
    bool insert_ports(sqlite3* h, const std::vector<PortRecordParams>& ports);
    bool insert_scan_ports(sqlite3* h, const std::vector<ScanPortParams>& ports);

    std::vector<ScanPortRow> list_scan_ports(sqlite3* h, int scan_id);
    std::vector<ScanPortRow> list_topology_open_ports(sqlite3* h, int scan_id);
    std::vector<PersistedPortStateRow> list_current_ports_for_host(sqlite3* h, int host_id);
};

#endif
