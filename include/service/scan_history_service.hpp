#ifndef SERVICE_SCAN_HISTORY_SERVICE_HPP
#define SERVICE_SCAN_HISTORY_SERVICE_HPP

#include "db/database.hpp"
#include "db/scan_repository.hpp"
#include "scan/scan_types.hpp"
#include "service/scan_history_types.hpp"

#include <memory>
#include <vector>

class ScanHistoryService
{
public:
    ScanHistoryService(Database& db, ScanRepository& repo);
    explicit ScanHistoryService(Database& db);

    std::unique_ptr<PersistedScanSummary> get_scan(int scan_id);
    std::vector<PersistedScanSummary>     list_all_scans();
    FilteredScansResult                   list_filtered_scans(const ScanFilter& filter);
    FilteredScansResult                   list_filtered_scans(
        const ScanFilter& filter, const std::vector<std::string>& allowed_targets);

private:
    Database&                        db_;
    std::unique_ptr<ScanRepository>  owned_repo_;
    ScanRepository&                  repo_;
};

#endif
