#include "service/scan_diff_service.hpp"

#include <memory>
#include <unordered_set>

namespace
{
const char* NEW_HOST_CATEGORY = "new_host";
const char* DISAPPEARED_HOST_CATEGORY = "disappeared_host";
const char* NEW_OPEN_PORT_CATEGORY = "new_open_port";
const char* DISAPPEARED_PORT_CATEGORY = "disappeared_port";

bool is_port_category(const std::string& category)
{
    return category == NEW_OPEN_PORT_CATEGORY || category == DISAPPEARED_PORT_CATEGORY;
}

bool is_host_category(const std::string& category)
{
    return category == NEW_HOST_CATEGORY || category == DISAPPEARED_HOST_CATEGORY;
}

ScanDiffAcknowledgementKey make_host_ack_key(const char* category, const HostDiffEntry& host)
{
    ScanDiffAcknowledgementKey key;
    key.category = category;
    key.ip = host.ip;
    return key;
}

ScanDiffAcknowledgementKey make_port_ack_key(const char* category, const PortDiffEntry& port)
{
    ScanDiffAcknowledgementKey key;
    key.category = category;
    key.ip = port.ip;
    key.port = port.port;
    key.has_port = true;
    return key;
}

template <typename Entry>
void apply_acknowledgement(Entry& entry, const ScanDiffAcknowledgementKey& key,
                           const std::unordered_set<std::string>& acknowledged)
{
    entry.acknowledgement_key = scan_diff_acknowledgement_key_id(key);
    entry.acknowledged =
        acknowledged.find(entry.acknowledgement_key) != acknowledged.end();
}

void annotate_host_entries(std::vector<HostDiffEntry>& entries, const char* category,
                           const std::unordered_set<std::string>& acknowledged)
{
    for (auto& entry : entries)
        apply_acknowledgement(entry, make_host_ack_key(category, entry), acknowledged);
}

void annotate_port_entries(std::vector<PortDiffEntry>& entries, const char* category,
                           const std::unordered_set<std::string>& acknowledged)
{
    for (auto& entry : entries)
        apply_acknowledgement(entry, make_port_ack_key(category, entry), acknowledged);
}

void annotate_diff(ScanDiffModel& diff, const std::unordered_set<std::string>& acknowledged)
{
    annotate_host_entries(diff.host_diff.new_hosts, NEW_HOST_CATEGORY, acknowledged);
    annotate_host_entries(diff.host_diff.disappeared_hosts, DISAPPEARED_HOST_CATEGORY,
                          acknowledged);
    annotate_port_entries(diff.port_diff.new_open_ports, NEW_OPEN_PORT_CATEGORY, acknowledged);
    annotate_port_entries(diff.port_diff.disappeared_ports, DISAPPEARED_PORT_CATEGORY,
                          acknowledged);
}

bool validate_acknowledgement_key(const ScanDiffAcknowledgementKey& key, std::string& error)
{
    if (!is_host_category(key.category) && !is_port_category(key.category))
    {
        error = "invalid acknowledgement category";
        return false;
    }

    if (key.ip.empty())
    {
        error = "field 'ip' must not be empty";
        return false;
    }

    if (is_host_category(key.category))
        return true;

    if (!key.has_port || key.port < 1 || key.port > 65535)
    {
        error = "field 'port' must be between 1 and 65535";
        return false;
    }

    return true;
}

ScanDiffAcknowledgementResult make_ack_error(const ScanDiffAcknowledgementKey& key,
                                             const std::string& message)
{
    ScanDiffAcknowledgementResult result;
    result.key = key;
    result.error = ScanDiffAcknowledgementError::BadRequest;
    result.error_message = message;
    return result;
}

ScanDiffAcknowledgementResult make_db_error(const ScanDiffAcknowledgementKey& key)
{
    ScanDiffAcknowledgementResult result;
    result.key = key;
    result.error = ScanDiffAcknowledgementError::DatabaseError;
    result.error_message = "failed to update diff acknowledgement";
    return result;
}

ScanDiffResult make_scan_diff_error(ScanDiffError error)
{
    ScanDiffResult result = {};
    result.error = error;
    return result;
}

bool is_diffable_scan(const PersistedScanSummary* scan)
{
    return scan && !scan->deleted && scan->state == PersistedScanState::Completed;
}

HostDiffResult load_host_diff(sqlite3* h, ScanDiffRepository& repo,
                               const PersistedScanSummary* baseline, int scan_id)
{
    if (!baseline)
        return HostDiffResult{};
    return repo.get_host_diff_between_scans(h, baseline->id, scan_id);
}

PortDiffResult load_port_diff(sqlite3* h, ScanDiffRepository& repo,
                               const PersistedScanSummary* baseline, int scan_id)
{
    if (!baseline)
        return PortDiffResult{};
    return repo.get_port_diff_between_scans(h, baseline->id, scan_id);
}

std::unique_ptr<ScanDiffModel> load_scan_diff_model(sqlite3* h, ScanRepository& repo, int scan_id)
{
    auto scan = repo.get_scan_by_id(h, scan_id);
    if (!is_diffable_scan(scan.get()))
        return nullptr;

    auto diff = std::make_unique<ScanDiffModel>();
    diff->scan = *scan;
    diff->baseline = repo.get_previous_comparable_completed_scan(h, scan_id);
    ScanDiffRepository& diff_repo = repo;
    diff->host_diff = load_host_diff(h, diff_repo, diff->baseline.get(), scan_id);
    diff->port_diff = load_port_diff(h, diff_repo, diff->baseline.get(), scan_id);
    annotate_diff(*diff, diff_repo.list_acknowledged_diff_keys(h));
    return diff;
}
} // namespace

ScanDiffService::ScanDiffService(Database& db)
  : db_(db),
    owned_repo_(std::make_unique<ScanRepository>(db)),
    repo_(*owned_repo_)
{
}

ScanDiffService::ScanDiffService(Database& db, ScanRepository& repo)
  : db_(db),
    owned_repo_(nullptr),
    repo_(repo)
{
}

ScanDiffResult ScanDiffService::get_scan_diff(int scan_id)
{
    if (scan_id <= 0)
        return make_scan_diff_error(ScanDiffError::NotFound);

    return db_.read([this, scan_id](sqlite3* h) {
        auto scan = repo_.get_scan_by_id(h, scan_id);
        if (!scan || scan->deleted)
            return make_scan_diff_error(ScanDiffError::NotFound);
        if (scan->state != PersistedScanState::Completed)
            return make_scan_diff_error(ScanDiffError::NotDiffable);

        ScanDiffResult result = {};
        result.diff = load_scan_diff_model(h, repo_, scan_id);
        result.error = ScanDiffError::None;
        return result;
    });
}

ScanDiffAcknowledgementResult ScanDiffService::apply_acknowledgement(
    const ScanDiffAcknowledgementKey& key, bool acknowledged)
{
    std::string error;
    if (!validate_acknowledgement_key(key, error))
        return make_ack_error(key, error);

    return db_.write(
        [this, key, acknowledged](sqlite3* h)
        {
            ScanDiffRepository& diff_repo = repo_;
            const bool ok = acknowledged ? diff_repo.set_diff_acknowledgement(h, key)
                                         : diff_repo.clear_diff_acknowledgement(h, key);
            if (!ok)
                return make_db_error(key);

            ScanDiffAcknowledgementResult result;
            result.key = key;
            result.acknowledged = acknowledged;
            return result;
        });
}

ScanDiffAcknowledgementResult ScanDiffService::acknowledge_diff(
    const ScanDiffAcknowledgementKey& key)
{
    return apply_acknowledgement(key, true);
}

ScanDiffAcknowledgementResult ScanDiffService::unacknowledge_diff(
    const ScanDiffAcknowledgementKey& key)
{
    return apply_acknowledgement(key, false);
}
