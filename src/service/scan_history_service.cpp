#include "service/scan_history_service.hpp"
#include "scan/target_validation.hpp"

#include <array>
#include <algorithm>

namespace
{
constexpr int k_restricted_scan_batch_size = 100;

bool is_valid_state(const std::string& state)
{
    static const std::array<const char*, 5> valid_states = {
        {"queued", "running", "completed", "failed", "aborted"}};
    return std::find(valid_states.begin(), valid_states.end(), state) != valid_states.end();
}

bool validate_filter(const ScanFilter& filter, std::string& error)
{
    if (!filter.state.empty() && !is_valid_state(filter.state))
    {
        error = "invalid state value: '" + filter.state +
                "'; expected one of: queued, running, completed, failed, aborted";
        return false;
    }
    if (filter.limit < 0)
    {
        error = "field 'limit' must be >= 0";
        return false;
    }
    if (filter.offset < 0)
    {
        error = "field 'offset' must be >= 0";
        return false;
    }
    return true;
}

std::vector<PersistedScanSummary> list_scoped_filtered_scans(
    sqlite3* h, ScanRepository& repo, const ScanFilter& filter,
    const std::vector<std::string>& allowed_targets)
{
    std::vector<PersistedScanSummary> scoped;
    int remaining_offset = filter.offset;
    const int result_limit = filter.limit;
    int db_offset = 0;

    for (;;)
    {
        ScanFilter batch_filter = filter;
        batch_filter.limit = k_restricted_scan_batch_size;
        batch_filter.offset = db_offset;

        std::vector<PersistedScanSummary> batch = repo.list_filtered_scans(h, batch_filter);
        if (batch.empty())
            break;

        db_offset += static_cast<int>(batch.size());

        for (std::vector<PersistedScanSummary>::const_iterator it = batch.begin();
             it != batch.end(); ++it)
        {
            if (!is_user_target_allowed(it->target, allowed_targets))
                continue;
            if (remaining_offset > 0)
            {
                --remaining_offset;
                continue;
            }
            if (result_limit > 0 && static_cast<int>(scoped.size()) >= result_limit)
                return scoped;
            scoped.push_back(*it);
        }

        if (static_cast<int>(batch.size()) < k_restricted_scan_batch_size)
            break;
    }

    return scoped;
}
} // namespace

ScanHistoryService::ScanHistoryService(Database& db)
    : db_(db), owned_repo_(std::make_unique<ScanRepository>(db)), repo_(*owned_repo_)
{
}

ScanHistoryService::ScanHistoryService(Database& db, ScanRepository& repo)
    : db_(db), owned_repo_(nullptr), repo_(repo)
{
}

std::unique_ptr<PersistedScanSummary> ScanHistoryService::get_scan(int scan_id)
{
    if (scan_id <= 0)
        return nullptr;

    return db_.read(
        [this, scan_id](sqlite3* h)
        {
            std::unique_ptr<PersistedScanSummary> scan = repo_.get_scan_by_id(h, scan_id);
            if (!scan || scan->deleted)
                return std::unique_ptr<PersistedScanSummary>();
            return scan;
        });
}

std::vector<PersistedScanSummary> ScanHistoryService::list_all_scans()
{
    return db_.read([this](sqlite3* h) { return repo_.list_all_scans(h); });
}

FilteredScansResult ScanHistoryService::list_filtered_scans(const ScanFilter& filter)
{
    std::string error;
    if (!validate_filter(filter, error))
        return {ListFilteredScansStatus::ValidationError, {}, error};

    FilteredScansResult result;
    result.scans = db_.read([this, &filter](sqlite3* h) {
        return repo_.list_filtered_scans(h, filter);
    });
    return result;
}

FilteredScansResult ScanHistoryService::list_filtered_scans(
    const ScanFilter& filter, const std::vector<std::string>& allowed_targets)
{
    std::string error;
    if (!validate_filter(filter, error))
        return {ListFilteredScansStatus::ValidationError, {}, error};

    FilteredScansResult result;
    result.scans = db_.read([this, &filter, &allowed_targets](sqlite3* h) {
        return list_scoped_filtered_scans(h, repo_, filter, allowed_targets);
    });
    return result;
}
