import { HOST_NODE_WIDTH } from "./layoutConstants.js";

// Custom hierarchical layout — no dagre needed.
// The graph is always shallow (2 levels: gateway → hosts), so a
// bespoke top-down placement is simpler and avoids an extra dependency.
//
// Port nodes are no longer rendered as standalone ReactFlow nodes.
// Instead, port data is embedded in each host node's data.ports array.
//
// Tunable constants:
const RANK_SEP = 132; // vertical distance between gateway and hosts
const NODE_SEP = 34;  // horizontal gap between sibling host nodes

// Host node width used for centering; must match the ReactFlow node width.
const NODE_WIDTH = HOST_NODE_WIDTH;

const TYPE_ORDER = { gateway: 0, host: 1, port: 2 };

// Minimum hosts in a /24 subnet before a cluster node is created.
const CLUSTER_MIN_HOSTS = 2;

function coerceText(value, fallback = "")
{
    return typeof value === "string" ? value : fallback;
}

function compareStrings(a, b)
{
    return a.localeCompare(b, undefined, { numeric: true, sensitivity: "base" });
}

function sanitizeNode(node)
{
    if (!node || typeof node.id !== "string" || typeof node.type !== "string")
        return null;

    if (!(node.type in TYPE_ORDER))
        return null;

    return {
        id: node.id,
        type: node.type,
        label: coerceText(node.label, node.id),
        meta: node.meta && typeof node.meta === "object" ? node.meta : {}
    };
}

function sanitizeEdge(edge)
{
    if (!edge || typeof edge.id !== "string" || typeof edge.source !== "string" || typeof edge.target !== "string")
        return null;

    return {
        id: edge.id,
        source: edge.source,
        target: edge.target
    };
}

function sortNodes(nodes)
{
    return [...nodes].sort((left, right) => {
        const typeDelta = (TYPE_ORDER[left.type] ?? 99) - (TYPE_ORDER[right.type] ?? 99);
        if (typeDelta !== 0)
            return typeDelta;

        return compareStrings(left.id, right.id);
    });
}

function sanitizeEdges(rawEdges, nodes)
{
    const validNodeIds = new Set(nodes.map(node => node.id));
    return rawEdges
        .filter(edge => validNodeIds.has(edge.source) && validNodeIds.has(edge.target));
}

function buildPortsForHost(hostId, allNodes, allEdges)
{
    const portIdSet = new Set(
        allEdges
            .filter(edge => edge.source === hostId)
            .map(edge => edge.target)
    );

    return allNodes
        .filter(node => node.type === "port" && portIdSet.has(node.id))
        .sort((a, b) => {
            const aPort = parseInt(coerceText(a.meta.port, "0"), 10);
            const bPort = parseInt(coerceText(b.meta.port, "0"), 10);
            return aPort - bPort;
        })
        .map(node => ({
            id: node.id,
            port: coerceText(node.meta.port, ""),
            protocol: coerceText(node.meta.protocol, ""),
            service: coerceText(node.meta.service, ""),
            state: coerceText(node.meta.state, "open")
        }));
}

function buildLevels(nodes, edges)
{
    const portCounts = new Map();
    edges.forEach(edge => {
        portCounts.set(edge.source, (portCounts.get(edge.source) || 0) + 1);
    });

    const gateways = nodes.filter(node => node.type === "gateway");
    const hosts = nodes.filter(node => node.type === "host");

    hosts.sort((left, right) => {
        const rightPorts = portCounts.get(right.id) || 0;
        const leftPorts = portCounts.get(left.id) || 0;
        if (rightPorts !== leftPorts)
            return rightPorts - leftPorts;

        return compareStrings(left.id, right.id);
    });

    return { gateways, hosts };
}

// Returns the total width of a horizontal row of N nodes at NODE_SEP gap.
function rowWidth(count)
{
    return count * NODE_WIDTH + Math.max(count - 1, 0) * NODE_SEP;
}

// Returns an array of x coordinates that centre N nodes around centerX.
function spreadX(count, centerX)
{
    const total = rowWidth(count);
    const startX = centerX - total / 2;
    return Array.from({ length: count }, (_, i) => startX + i * (NODE_WIDTH + NODE_SEP));
}

function assignPositions(levels)
{
    const positions = new Map();

    const centerX = 0;
    // Gateway node is narrower; use a fixed small width to centre it visually.
    const gatewayWidth = 160;
    levels.gateways.forEach((node, index) => {
        positions.set(node.id, { x: centerX - gatewayWidth / 2 + index * (gatewayWidth + NODE_SEP), y: 0 });
    });

    const hostCount = levels.hosts.length;
    const hostXs = spreadX(hostCount, centerX);
    const hostY = RANK_SEP;

    levels.hosts.forEach((host, index) => {
        positions.set(host.id, { x: hostXs[index], y: hostY });
    });

    return { positions };
}

function buildGatewayDetail(meta)
{
    return meta.host_discovery_only === "true" ? "host discovery only" : "port scan";
}

function buildHostName(meta)
{
    return coerceText(meta.name, "");
}

function buildHostIp(meta, fallback = "")
{
    return coerceText(meta.ip, fallback);
}

function readOpenPortCount(meta)
{
    const count = Number(meta?.openPortCount ?? meta?.open_port_count);
    return Number.isFinite(count) && count > 0 ? count : 0;
}

function buildGatewayNode(node, position)
{
    return {
        id: node.id,
        kind: node.type,
        eyebrow: "local",
        title: node.label,
        subtitle: "NetScan instance",
        detail: buildGatewayDetail(node.meta),
        ports: [],
        position,
        _meta: node.meta
    };
}

function buildHostNode(node, position, ports, localIp)
{
    const hostName = buildHostName(node.meta);
    const hostIp = buildHostIp(node.meta, node.label);
    const visibleOpenPortCount = ports.filter(port => port.state === "open").length;
    const openPortCount = ports.length > 0 ? visibleOpenPortCount : readOpenPortCount(node.meta);
    const isLocalInstance = localIp && localIp !== "0.0.0.0" && hostIp === localIp;

    return {
        id: node.id,
        kind: node.type,
        eyebrow: isLocalInstance ? "this instance" : "host",
        title: hostIp,
        subtitle: isLocalInstance ? "NetScan host" : (hostName || "unnamed host"),
        detail: isLocalInstance ? "netscan instance address" : (hostName ? "dns or persisted name" : "ip identity only"),
        ports,
        openPortCount,
        position,
        _meta: node.meta
    };
}

function buildDisplayNode(node, position, ports, localIp)
{
    if (node.type === "gateway")
        return buildGatewayNode(node, position);
    return buildHostNode(node, position, ports || [], localIp);
}

// Returns "192.168.1" for "192.168.1.42", null for non-IPv4 or unparseable.
function extractSlash24Prefix(ip)
{
    if (!ip || typeof ip !== "string")
        return null;
    const parts = ip.split(".");
    if (parts.length !== 4)
        return null;
    return parts.slice(0, 3).join(".");
}

function getHostIp(node)
{
    return coerceText(node.meta && node.meta.ip ? node.meta.ip : node.label, "");
}

// Group host nodes by /24. Returns Map<prefix, node[]>.
function groupBySubnet(hostNodes)
{
    const groups = new Map();
    hostNodes.forEach(node => {
        const ip = getHostIp(node);
        const prefix = extractSlash24Prefix(ip);
        const key = prefix || "__ungrouped__";
        if (!groups.has(key))
            groups.set(key, []);
        groups.get(key).push(node);
    });
    return groups;
}

function buildClusterNode(prefix, hosts, position)
{
    const clusterId = `cluster-${prefix}`;
    return {
        id: clusterId,
        kind: "cluster",
        eyebrow: "subnet",
        title: `${prefix}.0/24`,
        subtitle: `${hosts.length} hosts`,
        detail: "click to expand",
        ports: [],
        hostIds: hosts.map(h => h.id),
        position,
        _meta: {}
    };
}

// Replaces clusterable host nodes with cluster nodes.
// Returns { displayNodes, clusterHostMap, allHostDisplayNodes }.
// clusterHostMap: Map<clusterId, hostDisplayNode[]>
function applySubnetClustering(hostNodes, portsByHost, gatewayCount, localIp)
{
    const groups = groupBySubnet(hostNodes);
    const displayNodes = [];
    const clusterHostMap = new Map();
    const allHostDisplayNodes = [];

    // Build all host display nodes first (needed for expand lookup).
    const hostDisplayMap = new Map();
    hostNodes.forEach(node => {
        const dn = buildHostNode(node, { x: 0, y: 0 }, portsByHost.get(node.id) || [], localIp);
        hostDisplayMap.set(node.id, dn);
        allHostDisplayNodes.push(dn);
    });

    groups.forEach((hosts, prefix) => {
        if (hosts.length >= CLUSTER_MIN_HOSTS && prefix !== "__ungrouped__") {
            // Placeholder position — will be assigned by assignPositions.
            const cluster = buildClusterNode(prefix, hosts, { x: 0, y: 0 });
            displayNodes.push({ _raw: null, _cluster: cluster });
            clusterHostMap.set(cluster.id, hosts.map(h => hostDisplayMap.get(h.id)));
        } else {
            hosts.forEach(node => {
                displayNodes.push({ _raw: node, _cluster: null });
            });
        }
    });

    return { displayNodes, clusterHostMap, allHostDisplayNodes };
}

// Merges meta objects from an existing node and a newer node: newer keys win,
// but empty/missing values in the newer node never erase non-empty existing values.
// This preserves backend-only fields like `tags` when a synthetic payload
// (e.g. dashboard-derived) overwrites the same host id without those fields.
function mergeNodeMeta(existing, incoming)
{
    if (!existing || !existing.meta)
        return incoming;
    const existingMeta = existing.meta && typeof existing.meta === "object" ? existing.meta : {};
    const incomingMeta = incoming.meta && typeof incoming.meta === "object" ? incoming.meta : {};
    const mergedMeta = { ...existingMeta };
    Object.keys(incomingMeta).forEach(key => {
        const val = incomingMeta[key];
        if (val !== "" && val != null)
            mergedMeta[key] = val;
        else if (!(key in mergedMeta))
            mergedMeta[key] = val;
    });
    return { ...incoming, meta: mergedMeta };
}

// Merges multiple backend topology payloads into a single deduplicated payload.
// Nodes are deduplicated by id; edges are deduplicated by id.
// Gateway nodes (type="gateway") use last-wins to reflect the latest scan context.
// Host and port nodes use last-wins to reflect the latest discovered state.
// A node that is a gateway in any payload keeps its gateway type — it is never
// downgraded to host even if a host with the same id appears in another payload.
export function mergeTopologyPayloads(payloads)
{
    const nodeMap = new Map();
    const edgeMap = new Map();

    payloads.forEach(payload => {
        if (!payload)
            return;
        const nodes = Array.isArray(payload.nodes) ? payload.nodes : [];
        const edges = Array.isArray(payload.edges) ? payload.edges : [];

        nodes.forEach(node => {
            if (!node || typeof node.id !== "string")
                return;
            // Never overwrite a gateway node with a non-gateway node.
            // This prevents a scanned host from replacing the NetScan gateway node.
            const existing = nodeMap.get(node.id);
            if (existing && existing.type === "gateway" && node.type !== "gateway")
                return;
            nodeMap.set(node.id, mergeNodeMeta(existing, node));
        });

        edges.forEach(edge => {
            if (!edge || typeof edge.id !== "string")
                return;
            // Last-wins for edges to pick up the most recent state.
            edgeMap.set(edge.id, edge);
        });
    });

    return {
        nodes: Array.from(nodeMap.values()),
        edges: Array.from(edgeMap.values())
    };
}

export function normalizeTopologyGraph(payload, localIp = "")
{
    const rawNodes = Array.isArray(payload?.nodes) ? payload.nodes.map(sanitizeNode).filter(Boolean) : [];
    const allNodes = sortNodes(rawNodes);
    const rawEdges = Array.isArray(payload?.edges) ? payload.edges.map(sanitizeEdge).filter(Boolean) : [];
    const allEdges = sanitizeEdges(rawEdges, allNodes);

    // Build port data per host (using the full node/edge sets, before filtering).
    const portsByHost = new Map();
    allNodes
        .filter(node => node.type === "host")
        .forEach(host => {
            portsByHost.set(host.id, buildPortsForHost(host.id, allNodes, allEdges).filter(p => p.state === "open"));
        });

    // Exclude port nodes — they are now embedded in host cards.
    const structuralNodes = allNodes.filter(node => node.type !== "port");

    // Exclude host→port edges — only gateway→host edges remain.
    // Only edges from nodes explicitly typed as "gateway" count as gateway edges.
    const gatewayEdges = allEdges.filter(edge => {
        const sourceNode = allNodes.find(n => n.id === edge.source);
        return sourceNode != null && sourceNode.type === "gateway";
    });

    // Gateway nodes are exclusively those with type="gateway" from the backend.
    // Scanned hosts (type="host") are never promoted to gateway nodes.
    const gatewayNodes = structuralNodes.filter(n => n.type === "gateway");
    const hostNodes = structuralNodes.filter(n => n.type === "host");

    if (hostNodes.length === 0) {
        return {
            nodes: [],
            edges: [],
            clusterHostMap: new Map(),
            allHostNodes: []
        };
    }

    // Apply /24 subnet clustering.
    const { displayNodes, clusterHostMap, allHostDisplayNodes } =
        applySubnetClustering(hostNodes, portsByHost, gatewayNodes.length, localIp);

    // Build the flat list of nodes for layout: gateways + cluster/solo-host mix.
    // For layout purposes treat cluster nodes as "host" rank.
    const layoutHosts = displayNodes.map(entry =>
        entry._cluster
            ? { id: entry._cluster.id, type: "host" }   // cluster acts as host-rank node
            : entry._raw
    );

    const levels = buildLevels(
        [...gatewayNodes, ...layoutHosts],
        gatewayEdges   // port counts still used for sort; clusters have none — that's fine
    );
    const { positions } = assignPositions(levels);

    // Build final display node list (gateways + clusters + solo hosts).
    const finalNodes = [];

    gatewayNodes.forEach(node => {
        finalNodes.push(buildGatewayNode(node, positions.get(node.id) || { x: 0, y: 0 }));
    });

    displayNodes.forEach(entry => {
        if (entry._cluster) {
            const cluster = entry._cluster;
            finalNodes.push({ ...cluster, position: positions.get(cluster.id) || { x: 0, y: 0 } });
        } else {
            const node = entry._raw;
            finalNodes.push(buildHostNode(node, positions.get(node.id) || { x: 0, y: 0 }, portsByHost.get(node.id) || [], localIp));
        }
    });

    // Build edges: gateway → cluster (or gateway → solo host).
    // For clustered hosts, replace individual gateway→host edges with gateway→cluster.
    const hostIdToClusterId = new Map();
    clusterHostMap.forEach((hostDisplayNodes, clusterId) => {
        hostDisplayNodes.forEach(hn => hostIdToClusterId.set(hn.id, clusterId));
    });

    // Use the first gateway node as the source for all edges.
    // If no gateway node exists in the payload, edges cannot be built.
    const primaryGatewayId = gatewayNodes.length > 0 ? gatewayNodes[0].id : null;

    const seenEdgeTargets = new Set();
    const finalEdges = [];

    // Add edges from existing gateway→host payload edges.
    gatewayEdges.forEach(edge => {
        const mappedTarget = hostIdToClusterId.get(edge.target) || edge.target;
        const dedupeKey = `${edge.source}__${mappedTarget}`;
        if (seenEdgeTargets.has(dedupeKey))
            return;
        seenEdgeTargets.add(dedupeKey);
        finalEdges.push({ id: `${edge.source}->${mappedTarget}`, source: edge.source, target: mappedTarget });
    });

    // Defensive pass: ensure every display node (host or cluster) has a gateway edge.
    // This handles accumulated hosts from merged payloads that may lack explicit edges.
    if (primaryGatewayId != null) {
        displayNodes.forEach(entry => {
            const targetId = entry._cluster ? entry._cluster.id : (entry._raw ? entry._raw.id : null);
            if (!targetId)
                return;
            const mappedTarget = hostIdToClusterId.get(targetId) || targetId;
            const dedupeKey = `${primaryGatewayId}__${mappedTarget}`;
            if (seenEdgeTargets.has(dedupeKey))
                return;
            seenEdgeTargets.add(dedupeKey);
            finalEdges.push({ id: `${primaryGatewayId}->${mappedTarget}`, source: primaryGatewayId, target: mappedTarget });
        });
    }

    // allHostNodes: flat host display nodes with correct port data, used for cluster expansion.
    // Positions are not assigned here; TopologyCanvas will re-run layout on expand.
    const allHostNodes = allHostDisplayNodes;

    return {
        nodes: finalNodes,
        edges: finalEdges,
        clusterHostMap,
        allHostNodes
    };
}
