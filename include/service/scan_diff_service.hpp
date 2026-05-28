#ifndef SERVICE_SCAN_DIFF_SERVICE_HPP
#define SERVICE_SCAN_DIFF_SERVICE_HPP

#include "db/database.hpp"
#include "db/scan_repository.hpp"
#include "scan/scan_diff_types.hpp"

class ScanDiffService
{
public:
    ScanDiffService(Database& db, ScanRepository& repo);
    explicit ScanDiffService(Database& db);

    ScanDiffResult                   get_scan_diff(int scan_id);
    ScanDiffAcknowledgementResult    acknowledge_diff(const ScanDiffAcknowledgementKey& key);
    ScanDiffAcknowledgementResult    unacknowledge_diff(const ScanDiffAcknowledgementKey& key);

private:
    ScanDiffAcknowledgementResult    apply_acknowledgement(
        const ScanDiffAcknowledgementKey& key, bool acknowledged);

    Database&                    db_;
    std::unique_ptr<ScanRepository> owned_repo_;
    ScanRepository&              repo_;
};

#endif
