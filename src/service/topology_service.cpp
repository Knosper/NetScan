#include "service/topology_service.hpp"
#include "service/topology_builder.hpp"
#include "db/host_meta_repository.hpp"
#include <set>
#include <string>
#include <vector>

namespace
{

// Returns true only for scans that have persisted host/port data to build from.
bool is_topology_buildable(PersistedScanState state)
{
    return state == PersistedScanState::Completed;
}

std::string host_node_id(const std::string& ip)
{
    return "host::" + ip;
}

std::string port_node_id(const std::string& ip, int port, const std::string& proto)
{
    return "port::" + ip + "::" + std::to_string(port) + "/" + proto;
}

std::string edge_id(const std::string& src, const std::string& tgt)
{
    return src + "--" + tgt;
}

std::string port_label(int port, const std::string& proto, const std::string& service)
{
    std::string label = std::to_string(port) + "/" + proto;
    if (!service.empty())
        label += " (" + service + ")";
    return label;
}

lsm::service::TopologyNode make_gateway(const lsm::service::TopologyScanContext& ctx)
{
    lsm::service::TopologyNode n;
    n.id    = "gw";
    n.type  = lsm::service::TopologyNodeType::gateway;
    n.label = "NetScan";
    // Consumers use this flag to distinguish a port-scan topology (hosts + ports)
    // from a host-discovery-only topology (hosts only, no port nodes).
    n.meta["host_discovery_only"] = ctx.host_discovery_only ? "true" : "false";
    n.meta["scan_target"] = ctx.scan_target;
    return n;
}

lsm::service::TopologyNode make_host_node(const ScanHostRow& h, const HostMeta& meta)
{
    lsm::service::TopologyNode n;
    n.id        = host_node_id(h.ip);
    n.type      = lsm::service::TopologyNodeType::host;
    n.label     = h.name.empty() ? h.ip : h.name;
    n.meta["ip"]           = h.ip;
    n.meta["name"]         = h.name;
    n.meta["display_name"] = meta.display_name;
    n.meta["role"]         = meta.role;
    n.meta["tags"]         = meta.tags;
    return n;
}

lsm::service::TopologyNode make_port_node(const ScanPortRow& p)
{
    lsm::service::TopologyNode n;
    n.id        = port_node_id(p.host_ip, p.port, p.protocol);
    n.type      = lsm::service::TopologyNodeType::port;
    n.label     = port_label(p.port, p.protocol, p.service);
    n.meta["ip"]       = p.host_ip;
    n.meta["port"]     = std::to_string(p.port);
    n.meta["protocol"] = p.protocol;
    n.meta["state"]    = p.state;
    n.meta["service"]  = p.service;
    return n;
}

lsm::service::TopologyEdge make_edge(const std::string& src, const std::string& tgt)
{
    lsm::service::TopologyEdge e;
    e.id     = edge_id(src, tgt);
    e.source = src;
    e.target = tgt;
    return e;
}

std::set<std::string> collect_hosts_with_open_ports(const std::vector<ScanPortRow>& ports)
{
    std::set<std::string> hosts;
    for (const auto& port : ports)
    {
        if (port.state == "open")
            hosts.insert(port.host_ip);
    }
    return hosts;
}

bool should_include_host(const lsm::service::TopologyScanContext& ctx,
                         const ScanHostRow& host,
                         const std::set<std::string>& hosts_with_open_ports)
{
    if (ctx.host_discovery_only)
        return true;
    return hosts_with_open_ports.find(host.ip) != hosts_with_open_ports.end();
}

bool should_include_port(const lsm::service::TopologyScanContext& ctx,
                         const ScanPortRow& port,
                         const std::set<std::string>& hosts_with_open_ports)
{
    if (ctx.host_discovery_only)
        return false;
    if (port.state != "open")
        return false;
    return hosts_with_open_ports.find(port.host_ip) != hosts_with_open_ports.end();
}

lsm::service::TopologyResult make_topology_error_result(lsm::service::TopologyError error)
{
    lsm::service::TopologyResult result;
    result.error = error;
    return result;
}

std::vector<HostMeta> load_host_metas(sqlite3* h, Database& db,
                                      const std::vector<ScanHostRow>& hosts)
{
    HostMetaRepository meta_repo(db);
    std::vector<HostMeta> host_metas;
    host_metas.reserve(hosts.size());
    for (const auto& host : hosts)
        host_metas.push_back(meta_repo.get_meta(h, host.ip));
    return host_metas;
}

lsm::service::TopologyResult build_topology_result(sqlite3* h, Database& db, ScanRepository& repo,
                                                   int scan_id)
{
    auto scan = repo.get_scan_by_id(h, scan_id);
    if (!scan || scan->deleted)
        return make_topology_error_result(lsm::service::TopologyError::NotFound);
    if (!is_topology_buildable(scan->state))
        return make_topology_error_result(lsm::service::TopologyError::NotReady);

    lsm::service::TopologyScanContext ctx;
    ctx.scan_target = scan->target;
    ctx.host_discovery_only = scan->host_discovery_only;

    ScanHostRepository& host_repo = repo;
    ScanPortRepository& port_repo = repo;
    const std::vector<ScanHostRow> hosts = host_repo.list_scan_hosts(h, scan_id);
    const std::vector<ScanPortRow> ports = port_repo.list_topology_open_ports(h, scan_id);
    if (!ports.empty())
        ctx.host_discovery_only = false;

    lsm::service::TopologyResult result;
    result.payload = std::unique_ptr<lsm::service::TopologyPayload>(
        new lsm::service::TopologyPayload(
            lsm::service::build_topology(ctx, hosts, ports, load_host_metas(h, db, hosts))));
    return result;
}

} // namespace

namespace lsm {
namespace service {

TopologyPayload build_topology(const TopologyScanContext&        ctx,
                               const std::vector<ScanHostRow>&   hosts,
                               const std::vector<ScanPortRow>&   ports,
                               const std::vector<HostMeta>&      host_metas)
{
    TopologyPayload payload;
    const std::set<std::string> hosts_with_open_ports = collect_hosts_with_open_ports(ports);

    payload.nodes.push_back(make_gateway(ctx));

    for (std::size_t i = 0; i < hosts.size(); ++i)
    {
        const ScanHostRow& h = hosts[i];
        if (!should_include_host(ctx, h, hosts_with_open_ports))
            continue;

        const HostMeta& meta = i < host_metas.size() ? host_metas[i] : HostMeta{};
        payload.nodes.push_back(make_host_node(h, meta));
        payload.edges.push_back(make_edge("gw", host_node_id(h.ip)));
    }

    for (const auto& p : ports)
    {
        if (!should_include_port(ctx, p, hosts_with_open_ports))
            continue;

        payload.nodes.push_back(make_port_node(p));
        payload.edges.push_back(
            make_edge(host_node_id(p.host_ip), port_node_id(p.host_ip, p.port, p.protocol)));
    }

    return payload;
}

} // namespace service
} // namespace lsm

TopologyService::TopologyService(Database& db)
    : db_(db), owned_repo_(new ScanRepository(db)), repo_(*owned_repo_)
{
}

TopologyService::TopologyService(Database& db, ScanRepository& repo)
    : db_(db), owned_repo_(nullptr), repo_(repo)
{
}

lsm::service::TopologyResult TopologyService::get_topology(int scan_id)
{
    if (scan_id <= 0)
        return make_topology_error_result(lsm::service::TopologyError::NotFound);

    return db_.read(
        [this, scan_id](sqlite3* h) { return build_topology_result(h, db_, repo_, scan_id); });
}
