#ifndef DB_HOST_REPOSITORY_HPP
#define DB_HOST_REPOSITORY_HPP

#include "db/database.hpp"
#include "host/host_types.hpp"
#include <memory>
#include <string>
#include <vector>

struct HostHistoryPortRow
{
    int scan_id = 0;
    int port = 0;
    std::string service;
    std::string state;
};

struct HostListFilter
{
    std::string search;
    bool open_ports_only = false;
};

// Read-only repository.
// REQUIRES: caller passes the sqlite3* handle from the enclosing
// Database::read() access scope.
class HostRepository
{
public:
    explicit HostRepository(Database& db);

    std::vector<HostOverview> list_hosts_overview(sqlite3* h, const HostListFilter& filter,
                                                   int limit = 500, int offset = 0);
    int count_hosts_overview(sqlite3* h, const HostListFilter& filter);
    std::unique_ptr<HostSummary> get_host_by_ip(sqlite3* h, const std::string& ip);
    std::vector<HostHistoryScan> list_host_history(sqlite3* h, const std::string& ip);
    std::vector<HostHistoryPortRow> list_ports_for_host_history(sqlite3* h,
                                                                  const std::string& ip);
};

#endif
