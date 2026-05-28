#pragma once

// Topology domain model — v1
//
// Computed (derived) data. NOT persisted. Rebuilt on demand from scan/host data.
//
// ID scheme (deterministic, stable — suitable as ReactFlow node keys):
//   local node   : "gw"
//   host node    : "host::<ip>"                          e.g. "host::192.168.1.1"
//   port node    : "port::<ip>::<port>/<proto>"          e.g. "port::192.168.1.1::80/tcp"
//
// Edge ID : "<source_id>--<target_id>"
//
// v1 port semantics: port-scan topology hides hosts with no open ports.
// Visible hosts may include persisted ports across all states; state-based port
// filtering is a client presentation concern.
//
// The gateway node is synthetic — it represents the NS instance, not a
// scanned target or a real persisted host record.

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace lsm
{
namespace service
{

// Discriminator for the three node categories in v1.
enum class TopologyNodeType
{
    gateway,
    host,
    port
};

struct TopologyNode
{
    std::string id; // deterministic, see ID scheme above
    TopologyNodeType type;
    std::string label;

    // Optional metadata carried per node type:
    //   gateway : "host_discovery_only", "scan_target"
    //   host    : "ip", "name"
    //   port    : "ip", "port", "protocol", "service", "state"
    std::map<std::string, std::string> meta;
};

struct TopologyEdge
{
    std::string id; // "<source_id>--<target_id>"
    std::string source;
    std::string target;
};

// Top-level response container.
struct TopologyPayload
{
    std::vector<TopologyNode> nodes;
    std::vector<TopologyEdge> edges;
};

enum class TopologyError
{
    None,
    NotFound, // scan_id unknown or deleted
    NotReady  // scan exists but is not in completed state
};

struct TopologyResult
{
    std::unique_ptr<TopologyPayload> payload;
    TopologyError error = TopologyError::None;

    bool ok() const
    {
        return error == TopologyError::None;
    }
};

} // namespace service
} // namespace lsm
