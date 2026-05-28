#ifndef DB_SCAN_READ_TYPES_HPP
#define DB_SCAN_READ_TYPES_HPP

#include <string>

// Raw row types for scan-derived read models — no business logic, pure DB output.
// REQUIRES: caller executes within a Database-managed read() access scope.

struct ScanHostRow
{
    std::string ip;
    std::string name;
};

struct ScanPortRow
{
    std::string host_ip;
    int         port     = 0;
    std::string protocol;
    std::string state;
    std::string service;
};

struct ScanPortCoverageRow
{
    std::string protocol;
    int         start_port = 0;
    int         end_port = 0;
};

// Aggregated host summary for scan detail responses.
struct ScanHostSummary
{
    std::string ip;
    std::string hostname;
    int         open_port_count = 0;
};

#endif
