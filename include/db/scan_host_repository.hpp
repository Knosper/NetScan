#ifndef DB_SCAN_HOST_REPOSITORY_HPP
#define DB_SCAN_HOST_REPOSITORY_HPP

#include "db/scan_read_types.hpp"
#include <string>
#include <vector>

struct sqlite3;

struct HostRecordParams
{
    std::string ip;
    std::string name;
};

class ScanHostRepository
{
public:
    bool insert_host(sqlite3* h, const std::string& ip, const std::string& name);
    bool insert_scan_host(sqlite3* h, int scan_id, const std::string& ip,
                          const std::string& name);
    bool insert_hosts(sqlite3* h, const std::vector<HostRecordParams>& hosts);
    bool insert_scan_hosts(sqlite3* h, int scan_id,
                           const std::vector<HostRecordParams>& hosts);
    int get_host_id_by_ip(sqlite3* h, const std::string& ip);

    std::vector<ScanHostRow> list_scan_hosts(sqlite3* h, int scan_id);
};

#endif
