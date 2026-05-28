#include "scan/scan_snapshot_merger.hpp"

#include "scan/nmap_xml_parser.hpp"
#include <map>

namespace lsm {
namespace scan {
namespace
{
std::string port_key(const PortEntry& port)
{
    return port.protocol + "\n" + std::to_string(port.port);
}

void merge_port(std::vector<PortEntry>& ports, const PortEntry& incoming)
{
    const std::string incoming_key = port_key(incoming);
    for (PortEntry& existing : ports)
    {
        if (port_key(existing) == incoming_key)
        {
            existing = incoming;
            return;
        }
    }

    ports.push_back(incoming);
}

void merge_host(HostSnapshot& existing, const HostSnapshot& incoming)
{
    if (existing.hostname.empty() && !incoming.hostname.empty())
        existing.hostname = incoming.hostname;

    for (const PortEntry& port : incoming.ports)
        merge_port(existing.ports, port);
}

void merge_snapshot_hosts(ScanSnapshot& merged, std::map<std::string, std::size_t>& host_index,
                          const ScanSnapshot& incoming)
{
    for (const HostSnapshot& host : incoming.hosts)
    {
        if (host.ip.empty())
            continue;

        const auto it = host_index.find(host.ip);
        if (it != host_index.end())
        {
            merge_host(merged.hosts[it->second], host);
            continue;
        }

        host_index[host.ip] = merged.hosts.size();
        merged.hosts.push_back(host);
    }
}
} // namespace

ScanSnapshot merge_scan_snapshots(const std::vector<ScanSnapshot>& snapshots)
{
    ScanSnapshot merged;
    std::map<std::string, std::size_t> host_index;
    for (const ScanSnapshot& snapshot : snapshots)
        merge_snapshot_hosts(merged, host_index, snapshot);
    return merged;
}

ScanSnapshotMergeResult merge_nmap_xml_snapshots(const std::vector<std::string>& xml_outputs)
{
    std::vector<ScanSnapshot> snapshots;
    snapshots.reserve(xml_outputs.size());

    for (const std::string& output : xml_outputs)
    {
        NmapXmlParseResult parse_result = parse_nmap_xml(output);
        if (!parse_result.ok)
            return {false, ScanSnapshot(), parse_result.error};
        snapshots.push_back(parse_result.snapshot);
    }

    return {true, merge_scan_snapshots(snapshots), ""};
}

} // namespace scan
} // namespace lsm
