#ifndef DB_SCAN_REPOSITORY_HPP
#define DB_SCAN_REPOSITORY_HPP

#include "db/database.hpp"
#include "scan/scan_types.hpp"
#include "service/scan_history_types.hpp"
#include <memory>
#include <string>
#include <vector>

#include "db/scan_coverage_repository.hpp"
#include "db/scan_diff_repository.hpp"
#include "db/scan_host_repository.hpp"
#include "db/scan_port_repository.hpp"
// Repository for persisted scan metadata.
// REQUIRES: caller passes the sqlite3* handle from the enclosing
// Database::read()/write() access scope.
class ScanRepository : public ScanCoverageRepository,
                       public ScanDiffRepository,
                       public ScanHostRepository,
                       public ScanPortRepository
{
public:
    explicit ScanRepository(Database& db);

    int insert_scan_queued(sqlite3* h, const ScanRequest& request);
    bool mark_scan_running(sqlite3* h, int scan_id, const std::string& started_at);
    bool mark_scan_aborted(sqlite3* h, int scan_id, const ScanResult& result,
                           const std::string& finished_at);
    bool mark_scan_completed(sqlite3* h, int scan_id, const ScanResult& result,
                             const std::string& finished_at);
    bool mark_scan_failed(sqlite3* h, int scan_id, const ScanResult& result,
                          const std::string& finished_at, bool require_running = false);
    bool mark_scan_dependency_missing(sqlite3* h, int scan_id, const ScanResult& result,
                                      const std::string& finished_at);
    int abort_incomplete_scans(sqlite3* h, const std::string& message,
                               const std::string& finished_at);

    std::unique_ptr<PersistedScanSummary> get_scan_by_id(sqlite3* h, int scan_id);
    bool soft_delete_scan_by_id(sqlite3* h, int scan_id, const std::string& deleted_at);
    std::unique_ptr<PersistedScanSummary> get_latest_scan(sqlite3* h);
    bool scan_exists_for_target(sqlite3* h, const std::string& target);
    std::unique_ptr<PersistedScanSummary> get_last_completed_scan_for_target(
        sqlite3* h, const std::string& target);
    std::vector<PersistedScanSummary> list_all_scans(sqlite3* h);
    std::vector<PersistedScanSummary> list_filtered_scans(sqlite3* h,
                                                           const ScanFilter& filter);
    std::unique_ptr<PersistedScanSummary> get_previous_comparable_completed_scan(
        sqlite3* h, int scan_id);
};
#endif
