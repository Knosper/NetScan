#include "service/scan_persistence.hpp"

#include "util/logger.hpp"
#include "util/string_utils.hpp"
#include <set>

namespace
{
struct HostPersistenceContext
{
    sqlite3*             h;
    ScanHostRepository&  repo;
    Logger&              logger;
    const char*          host_error_message;
    const char*          snapshot_error_message;
};

struct PortPersistenceContext
{
    sqlite3*             h;
    ScanHostRepository&  host_repo;
    ScanPortRepository&  port_repo;
    Logger&              logger;
};

struct HostPortSnapshot
{
    int                      host_id = -1;
    std::string              host_ip;
    std::vector<PortRecordParams> open_ports;
    std::set<std::string>    open_port_keys;
};

bool persist_scan_hosts(const HostPersistenceContext& context, int scan_id,
                        const std::vector<lsm::scan::HostSnapshot>& hosts)
{
    std::vector<HostRecordParams> records;
    records.reserve(hosts.size());

    for (const auto& host : hosts)
    {
        if (host.ip.empty())
        {
            context.logger.warn("skipping host entry with empty ip, ignoring");
            continue;
        }

        records.push_back(HostRecordParams{host.ip, host.hostname});
    }

    if (!context.repo.insert_hosts(context.h, records))
    {
        context.logger.error(context.host_error_message);
        return false;
    }

    if (!context.repo.insert_scan_hosts(context.h, scan_id, records))
    {
        context.logger.error(context.snapshot_error_message);
        return false;
    }

    return true;
}

std::string make_port_key(int port, const std::string& protocol)
{
    return protocol + "\n" + std::to_string(port);
}

bool is_port_in_range(int port, const PortCoverageRange& range)
{
    return port >= range.start_port && port <= range.end_port;
}

bool coverage_includes_port(const ScanPortCoverage& coverage, int port,
                             const std::string& protocol)
{
    if (!coverage.known || protocol != "tcp")
        return false;

    for (std::vector<PortCoverageRange>::const_iterator it = coverage.tcp_ranges.begin();
         it != coverage.tcp_ranges.end(); ++it)
    {
        if (is_port_in_range(port, *it))
            return true;
    }

    return false;
}

int resolve_port_host_id(const PortPersistenceContext& context, const std::string& ip)
{
    if (!util::is_numeric_ip_literal(ip))
    {
        context.logger.error("invalid numeric host key for port snapshot: " + ip);
        return -1;
    }

    // GUARANTEE: Host has already been created in persist_scan_hosts() within the same transaction
    // We are running inside an active BEGIN IMMEDIATE transaction with exclusive write lock
    // No other thread can modify host entries during this time
    int host_id = context.host_repo.get_host_id_by_ip(context.h, ip);

    // Fallback only for the unlikely case that the host no longer exists
    // This should never happen under normal operation
    if (host_id < 0)
    {
        context.logger.warn("host missing during port persistence, recreating: " + ip);
        if (!context.host_repo.insert_host(context.h, ip, ""))
        {
            context.logger.error("failed to persist host for port: " + ip);
            return -1;
        }
        host_id = context.host_repo.get_host_id_by_ip(context.h, ip);
    }

    return host_id;
}

bool is_valid_snapshot_port(const lsm::scan::PortEntry& port)
{
    return port.port > 0 && port.port <= 65535;
}

void append_scan_port_record(int scan_id, const std::string& host_ip,
                             const lsm::scan::PortEntry& port,
                             std::vector<ScanPortParams>& scan_port_records)
{
    scan_port_records.push_back(ScanPortParams{
        scan_id,
        host_ip,
        port.port,
        port.protocol,
        port.state,
        port.service
    });
}

void append_open_port_record(int scan_id, int host_id, const lsm::scan::PortEntry& port,
                             HostPortSnapshot& snapshot)
{
    if (port.state != "open")
        return;

    snapshot.open_port_keys.insert(make_port_key(port.port, port.protocol));
    snapshot.open_ports.push_back(PortRecordParams{
        host_id,
        port.port,
        port.protocol,
        port.state,
        port.service,
        scan_id,
        scan_id
    });
}

void append_host_scan_port_records(int scan_id, const lsm::scan::HostSnapshot& host,
                                   HostPortSnapshot& snapshot,
                                   std::vector<ScanPortParams>& scan_port_records)
{
    for (std::vector<lsm::scan::PortEntry>::const_iterator port_it = host.ports.begin();
         port_it != host.ports.end(); ++port_it)
    {
        if (!is_valid_snapshot_port(*port_it))
            continue;

        append_scan_port_record(scan_id, host.ip, *port_it, scan_port_records);
        append_open_port_record(scan_id, snapshot.host_id, *port_it, snapshot);
    }
}

bool build_host_port_snapshots(const PortPersistenceContext& context, int scan_id,
                               const std::vector<lsm::scan::HostSnapshot>& hosts,
                               std::vector<HostPortSnapshot>& snapshots,
                               std::vector<ScanPortParams>& scan_port_records)
{
    snapshots.clear();
    scan_port_records.clear();
    snapshots.reserve(hosts.size());

    for (std::vector<lsm::scan::HostSnapshot>::const_iterator host_it = hosts.begin();
         host_it != hosts.end(); ++host_it)
    {
        const int host_id = resolve_port_host_id(context, host_it->ip);
        if (host_id < 0)
            return false;

        HostPortSnapshot snapshot;
        snapshot.host_id = host_id;
        snapshot.host_ip = host_it->ip;

        append_host_scan_port_records(scan_id, *host_it, snapshot, scan_port_records);

        snapshots.push_back(snapshot);
    }

    return true;
}

bool persist_historical_scan_ports(const PortPersistenceContext& context,
                                   const std::vector<ScanPortParams>& scan_port_records)
{
    if (!context.port_repo.insert_scan_ports(context.h, scan_port_records))
    {
        context.logger.error("failed to persist scan port snapshots");
        return false;
    }

    return true;
}

bool persist_open_port_states(const PortPersistenceContext& context,
                              const std::vector<HostPortSnapshot>& snapshots)
{
    std::vector<PortRecordParams> open_port_records;
    for (std::vector<HostPortSnapshot>::const_iterator it = snapshots.begin();
         it != snapshots.end(); ++it)
    {
        open_port_records.insert(open_port_records.end(),
                                  it->open_ports.begin(), it->open_ports.end());
    }

    if (!context.port_repo.insert_ports(context.h, open_port_records))
    {
        context.logger.error("failed to persist discovered ports");
        return false;
    }

    return true;
}

bool persist_covered_closed_port_states(const PortPersistenceContext& context, int scan_id,
                                        const ScanPortCoverage& coverage,
                                        const std::vector<HostPortSnapshot>& snapshots)
{
    if (!coverage.known)
        return true;

    std::vector<PortRecordParams> closed_port_records;
    for (std::vector<HostPortSnapshot>::const_iterator host_it = snapshots.begin();
         host_it != snapshots.end(); ++host_it)
    {
        const std::vector<PersistedPortStateRow> current_ports =
            context.port_repo.list_current_ports_for_host(context.h, host_it->host_id);
        for (std::vector<PersistedPortStateRow>::const_iterator port_it = current_ports.begin();
             port_it != current_ports.end(); ++port_it)
        {
            if (!coverage_includes_port(coverage, port_it->port, port_it->protocol))
                continue;

            if (host_it->open_port_keys.find(make_port_key(port_it->port, port_it->protocol)) !=
                host_it->open_port_keys.end())
            {
                continue;
            }

            closed_port_records.push_back(PortRecordParams{
                port_it->host_id,
                port_it->port,
                port_it->protocol,
                "closed",
                port_it->service,
                port_it->last_open_scan_id,
                scan_id
            });
        }
    }

    if (!context.port_repo.insert_ports(context.h, closed_port_records))
    {
        context.logger.error("failed to persist covered closed port states");
        return false;
    }

    return true;
}

bool persist_scan_port_coverage(sqlite3* h, ScanCoverageRepository& repo, Logger& logger,
                                int scan_id,
                                const ScanPortCoverage& coverage)
{
    if (repo.replace_scan_port_coverage(h, scan_id, coverage))
        return true;

    logger.error("failed to persist scan port coverage");
    return false;
}
} // namespace

bool persist_completed_scan_snapshot(sqlite3* h, ScanRepository& repo, Logger& logger,
                                     const CompletedScanSnapshot& snapshot)
{
    const HostPersistenceContext host_context = {
        h,
        repo,
        logger,
        "failed to persist discovered host: ",
        "failed to persist scan host snapshot: "
    };
    if (!persist_scan_hosts(host_context, snapshot.scan_id, snapshot.snapshot.hosts))
        return false;

    if (!persist_scan_port_coverage(h, repo, logger, snapshot.scan_id, snapshot.port_coverage))
        return false;

    if (snapshot.host_discovery_only)
        return true;

    const PortPersistenceContext port_context = {h, repo, repo, logger};
    std::vector<HostPortSnapshot> host_snapshots;
    std::vector<ScanPortParams> scan_port_records;
    if (!build_host_port_snapshots(port_context, snapshot.scan_id, snapshot.snapshot.hosts,
                                   host_snapshots, scan_port_records))
        return false;

    if (!persist_historical_scan_ports(port_context, scan_port_records))
        return false;

    if (!persist_open_port_states(port_context, host_snapshots))
        return false;

    return persist_covered_closed_port_states(port_context, snapshot.scan_id,
                                              snapshot.port_coverage, host_snapshots);
}
