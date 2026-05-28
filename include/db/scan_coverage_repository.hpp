#ifndef DB_SCAN_COVERAGE_REPOSITORY_HPP
#define DB_SCAN_COVERAGE_REPOSITORY_HPP

#include "db/scan_read_types.hpp"
#include "scan/scan_types.hpp"
#include <string>
#include <vector>

struct sqlite3;

class ScanCoverageRepository
{
public:
    bool replace_scan_port_coverage(sqlite3* h, int scan_id, const ScanPortCoverage& coverage);
    std::vector<ScanPortCoverageRow> list_scan_port_coverage(sqlite3* h, int scan_id);
    bool scan_has_known_port_coverage(sqlite3* h, int scan_id);
    bool scan_covers_port(sqlite3* h, int scan_id, int port,
                          const std::string& protocol = "tcp");
};

#endif
