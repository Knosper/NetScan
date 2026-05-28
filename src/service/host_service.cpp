#include "service/host_service.hpp"

#include <unordered_map>

namespace
{
std::unordered_map<int, std::vector<HostHistoryPort> > group_ports_by_scan(
    const std::vector<HostHistoryPortRow>& rows)
{
    std::unordered_map<int, std::vector<HostHistoryPort> > ports_by_scan_id;
    for (const auto& row : rows)
    {
        HostHistoryPort port;
        port.port = row.port;
        port.service = row.service;
        port.state = row.state;
        ports_by_scan_id[row.scan_id].push_back(port);
    }
    return ports_by_scan_id;
}

std::vector<HostHistoryEntry> assemble_history(
    const std::vector<HostHistoryScan>& history,
    const std::unordered_map<int, std::vector<HostHistoryPort> >& ports_by_scan_id)
{
    std::vector<HostHistoryEntry> entries;
    for (const auto& scan : history)
    {
        HostHistoryEntry entry;
        entry.scan = scan;
        auto it = ports_by_scan_id.find(scan.id);
        if (it != ports_by_scan_id.end())
            entry.ports = it->second;
        entries.push_back(entry);
    }
    return entries;
}
} // namespace

HostService::HostService(Database& db) : db_(db), repo_(db), meta_repo_(db), scan_repo_(db) {}

std::vector<HostOverview> HostService::list_hosts(const HostListFilter& filter, int limit,
                                                    int offset)
{
    return db_.read([this, &filter, limit, offset](sqlite3* h) {
        return repo_.list_hosts_overview(h, filter, limit, offset);
    });
}

int HostService::count_hosts(const HostListFilter& filter)
{
    return db_.read([this, &filter](sqlite3* h) { return repo_.count_hosts_overview(h, filter); });
}

int HostService::prune_closed_ports()
{
    return db_.write([this](sqlite3* h) { return scan_repo_.prune_closed_ports(h); });
}

std::unique_ptr<HostDetail> HostService::get_host_detail(const std::string& ip)
{
    return db_.read(
        [this, &ip](sqlite3* h)
        {
            std::unique_ptr<HostSummary> host = repo_.get_host_by_ip(h, ip);
            if (!host)
                return std::unique_ptr<HostDetail>();

            std::unique_ptr<HostDetail> detail(new HostDetail());
            detail->host = *host;
            detail->meta = meta_repo_.get_meta(h, ip);

            const auto ports_by_scan_id =
                group_ports_by_scan(repo_.list_ports_for_host_history(h, ip));
            detail->history = assemble_history(repo_.list_host_history(h, ip), ports_by_scan_id);
            return detail;
        });
}

void HostService::update_host_meta(const std::string& ip, const HostMeta& meta)
{
    db_.write([this, &ip, &meta](sqlite3* h) { meta_repo_.upsert_meta(h, ip, meta); });
}

void HostService::clear_host_meta_field(const std::string& ip, const std::string& field)
{
    db_.write([this, &ip, &field](sqlite3* h) { meta_repo_.clear_field(h, ip, field); });
}
