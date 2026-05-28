#pragma once

// Declares the topology builder function.
//
// Relationships created (v1):
//   local    --> host   : one edge per visible host in the topology history
//   host     --> port   : one edge per persisted port record on that visible host
//
// For port scans, hosts without at least one open port are hidden from the
// topology presentation. This is a read/presentation filter only; persisted scan
// host rows remain complete. Once a host is visible, its persisted port rows are
// included as-is, and any state-based port filtering happens in the client.
// The gateway node is synthetic — it represents the local LSM instance, not the
// scanned target and not a host record.
//
// Host-discovery-only scans ARE supported in v1:
//   ports will be empty → topology contains gateway + host nodes, no port nodes.
//   ctx.host_discovery_only == true is recorded in the gateway meta so consumers
//   can distinguish a port-scan topology from a discovery-only topology.
//
// Input is raw DB row data loaded via ScanRepository::list_scan_hosts() and
// list_scan_ports(). No live ScanSnapshot is required; the builder works on
// any completed scan retrieved from the database.

#include "db/scan_read_types.hpp"
#include "host/host_types.hpp"
#include "service/topology_model.hpp"
#include <string>
#include <vector>

namespace lsm {
namespace service {

// Scan-level context passed to build_topology.
// Carries the fields from PersistedScanSummary that drive graph structure.
struct TopologyScanContext
{
    std::string scan_target;
    bool        host_discovery_only = false;
};

// Builds a complete TopologyPayload from persisted scan data.
// host_metas must be parallel to hosts (same order, same size).
TopologyPayload build_topology(const TopologyScanContext&        ctx,
                               const std::vector<ScanHostRow>&   hosts,
                               const std::vector<ScanPortRow>&   ports,
                               const std::vector<HostMeta>&      host_metas);

} // namespace service
} // namespace lsm
