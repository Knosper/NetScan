#ifndef DB_SCAN_DIFF_REPOSITORY_HPP
#define DB_SCAN_DIFF_REPOSITORY_HPP

#include "scan/scan_diff_types.hpp"
#include <string>
#include <unordered_set>

struct sqlite3;

class ScanDiffRepository
{
public:
    HostDiffResult get_host_diff_between_scans(sqlite3* h, int baseline_scan_id,
                                               int current_scan_id);
    PortDiffResult get_port_diff_between_scans(sqlite3* h, int baseline_scan_id,
                                               int current_scan_id);
    std::unordered_set<std::string> list_acknowledged_diff_keys(sqlite3* h);
    bool set_diff_acknowledgement(sqlite3* h, const ScanDiffAcknowledgementKey& key);
    bool clear_diff_acknowledgement(sqlite3* h, const ScanDiffAcknowledgementKey& key);
};

#endif
