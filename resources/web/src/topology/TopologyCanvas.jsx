import React, { useMemo, useState, useCallback, useRef, useEffect } from "react";
import ReactFlow, { Background, BaseEdge, MarkerType, Panel, useNodesInitialized, useReactFlow } from "reactflow";
import { HOST_NODE_WIDTH } from "./layoutConstants.js";
import { parseTags } from "../ui/tagUtils.js";

const TOOLTIP_OFFSET_X = 14;
const TOOLTIP_OFFSET_Y = -8;
const EXPANDED_NODE_WIDTH = HOST_NODE_WIDTH;
const EXPANDED_NODE_GAP = 22;
const FIT_PADDING = 0.12;
const FIT_MIN_ZOOM = 0.2;

function TopologyLegend({ flags }) {
    const { hasGateway, hasHosts, hasClusters, hasNewHosts, hasGoneHosts, hasTrackedUp, hasTrackedPaused, hasTrackedDown } = flags || {};
    const rows = [];

    if (hasGateway) {
        rows.push(
            <div key="gateway" className="topology-legend-row">
                <span className="topology-legend-node topology-legend-node-root" aria-hidden="true">
                    <span className="topology-legend-node-core" />
                </span>
                <span className="topology-legend-label">NetScan</span>
            </div>
        );
    }

    if (hasHosts) {
        rows.push(
            <div key="host" className="topology-legend-row">
                <span className="topology-legend-node topology-legend-node-host" aria-hidden="true">
                    <span className="topology-legend-node-core" />
                </span>
                <span className="topology-legend-label">Host</span>
            </div>
        );
    }

    if (hasClusters) {
        rows.push(
            <div key="cluster" className="topology-legend-row">
                <span className="topology-legend-node topology-legend-node-host" aria-hidden="true">
                    <span className="topology-legend-node-core" />
                </span>
                <span className="topology-legend-label">Subnet cluster</span>
            </div>
        );
    }

    if (hasNewHosts) {
        rows.push(
            <div key="new" className="topology-legend-row">
                <span className="topology-legend-state topology-legend-state-new">new</span>
                <span className="topology-legend-label">new host</span>
            </div>
        );
    }

    if (hasGoneHosts) {
        rows.push(
            <div key="gone" className="topology-legend-row">
                <span className="topology-legend-state topology-legend-state-disappeared">gone</span>
                <span className="topology-legend-label">disappeared host</span>
            </div>
        );
    }

    if (hasTrackedUp) {
        rows.push(
            <div key="tracked-up" className="topology-legend-row">
                <span className="topo-node-presence-dot topo-node-presence-dot-up topology-legend-presence-dot" aria-hidden="true" />
                <span className="topology-legend-label">online</span>
            </div>
        );
    }

    if (hasTrackedPaused) {
        rows.push(
            <div key="tracked-paused" className="topology-legend-row">
                <span className="topo-node-presence-dot topo-node-presence-dot-paused topology-legend-presence-dot" aria-hidden="true" />
                <span className="topology-legend-label">tracking paused</span>
            </div>
        );
    }

    if (hasTrackedDown) {
        rows.push(
            <div key="tracked-down" className="topology-legend-row">
                <span className="topo-node-presence-dot topo-node-presence-dot-down topology-legend-presence-dot" aria-hidden="true" />
                <span className="topology-legend-label">offline</span>
            </div>
        );
    }

    if (rows.length === 0)
        return null;

    return (
        <Panel position="top-right">
            <div className="topology-legend" aria-label="Topology legend">
                {rows}
            </div>
        </Panel>
    );
}

function buildTooltipRows(graphNode) {
    if (!graphNode)
        return [];

    const kind = graphNode.kind;
    const meta = graphNode._meta || {};

    if (kind === "gateway") {
        const rows = [];
        if (meta.scan_target)
            rows.push({ key: "target", val: meta.scan_target });
        rows.push({ key: "type", val: graphNode.detail || "port scan" });
        return rows;
    }

    if (kind === "host") {
        const rows = [];
        const os = meta.os || "";
        const lastSeen = meta.last_seen || meta.lastSeen || "";
        const openPorts = Number.isFinite(graphNode.openPortCount)
            ? String(graphNode.openPortCount)
            : meta.open_port_count != null
            ? String(meta.open_port_count)
            : (meta.openPortCount != null ? String(meta.openPortCount) : "");
        const tags = parseTags(meta.tags || "");

        if (os)
            rows.push({ key: "os", val: os });
        if (lastSeen)
            rows.push({ key: "last seen", val: lastSeen });
        if (openPorts)
            rows.push({ key: "open ports", val: openPorts });
        if (tags.length > 0)
            rows.push({ key: "tags", val: tags.map(t => t.name).join(", ") });
        if (rows.length === 0)
            rows.push({ key: "ip", val: graphNode.title });
        return rows;
    }

    return [];
}

function NodeTooltip({ node, x, y }) {
    if (!node)
        return null;

    const rows = buildTooltipRows(node);
    if (rows.length === 0)
        return null;

    return (
        <div
            className="topo-tooltip"
            style={{ left: x + TOOLTIP_OFFSET_X, top: y + TOOLTIP_OFFSET_Y }}
        >
            <div className="topo-tooltip-label">{node.eyebrow}</div>
            {rows.map(row => (
                <div key={row.key} className="topo-tooltip-row">
                    <span className="topo-tooltip-key">{row.key}</span>
                    <span className="topo-tooltip-val">{row.val}</span>
                </div>
            ))}
        </div>
    );
}

function TopoHud({ onResetTopology }) {
    const { zoomIn, zoomOut, fitView } = useReactFlow();
    return (
        <Panel position="bottom-left">
            <div className="topo-hud">
                <div className="topo-hud-zoom">
                    <button type="button" className="topo-hud-btn" onClick={() => zoomIn({ duration: 200 })}>+</button>
                    <button type="button" className="topo-hud-btn" onClick={() => zoomOut({ duration: 200 })}>−</button>
                    <button type="button" className="topo-hud-btn" onClick={() => fitView({ padding: 0.18, duration: 200 })}>⊡</button>
                </div>
                {onResetTopology && (
                    <button
                        type="button"
                        className="topo-hud-toggle"
                        onClick={onResetTopology}
                        title="Reset accumulated topology view"
                    >
                        reset
                    </button>
                )}
            </div>
        </Panel>
    );
}

const EDGE_MARKER = {
    type: MarkerType.ArrowClosed,
    width: 5,
    height: 5
};

function countOpenPorts(node)
{
    if (!node || node.kind !== "host")
        return 0;

    if (Number.isFinite(node.openPortCount))
        return node.openPortCount;

    return Array.isArray(node.ports) ? node.ports.filter(port => port.state === "open").length : 0;
}

function getHostIp(node)
{
    if (!node || node.kind !== "host")
        return "";

    const meta = node._meta || {};
    if (typeof meta.ip === "string" && meta.ip)
        return meta.ip;

    return typeof node.title === "string" ? node.title : "";
}

function getNodeState(node, hostStateByIp)
{
    if (!node)
        return "";

    if (node.kind === "gateway")
        return "root";

    if (node.kind !== "host")
        return "";

    const hostState = hostStateByIp ? hostStateByIp.get(getHostIp(node)) : "";
    if (hostState === "new")
        return "new";
    if (hostState === "gone")
        return "disappeared";

    return countOpenPorts(node) > 0 ? "open" : "quiet";
}

function HostTagBadges({ meta })
{
    const tags = parseTags((meta && meta.tags) || "");
    if (tags.length === 0)
        return null;

    return (
        <div className="topo-node-tags">
            {tags.map(tag => (
                <span
                    key={tag.name}
                    className={`topo-node-tag-chip${tag.color ? ` host-tag-color-${tag.color}` : ""}`}
                >
                    {tag.name}
                </span>
            ))}
        </div>
    );
}

function HostStatusBadge({ state })
{
    if (!state || state === "open" || state === "quiet")
        return null;

    const label = state === "disappeared" ? "disappeared" : state;

    return (
        <span className={`topo-node-badge topo-node-badge-${state}`}>
            {label}
        </span>
    );
}

function OpenPortCount({ node, state }) {
    if (!node || node.kind !== "host")
        return null;

    const count = countOpenPorts(node);
    const label = count > 0 ? `${count} open` : "no open ports";

    return <span className={`topo-node-count topo-node-count-${state || "quiet"}`}>{label}</span>;
}

function formatPortLabel(port)
{
    if (!port)
        return "";

    const portNumber = typeof port.port === "string" ? port.port : "";
    const protocol = typeof port.protocol === "string" && port.protocol ? `/${port.protocol}` : "";
    const service = typeof port.service === "string" && port.service ? ` ${port.service}` : "";

    return `${portNumber}${protocol}${service}`.trim();
}

function SelectedNodeDetails({ node })
{
    if (!node)
        return null;

    const ports = Array.isArray(node.ports) ? node.ports : [];
    const meta = node._meta || {};
    const tags = node.kind === "host" ? parseTags(meta.tags || "") : [];

    return (
        <div className="topology-selection-summary">
            <div className="topology-selection-main">
                <span className={`topology-selection-kind topology-selection-kind-${node.kind}`}>
                    {node.eyebrow}
                </span>
                <span className="topology-selection-title">{node.title}</span>
                {node.subtitle ? <span className="topology-selection-detail">{node.subtitle}</span> : null}
            </div>
            {tags.length > 0 ? (
                <div className="topology-selection-tags">
                    {tags.map(tag => (
                        <span
                            key={tag.name}
                            className={`chip host-tag-chip${tag.color ? ` host-tag-color-${tag.color}` : ""}`}
                        >
                            {tag.name}
                        </span>
                    ))}
                </div>
            ) : null}
            {node.kind === "host" ? (
                <div className="topology-selection-ports">
                    <div className="topology-selection-ports-head">
                        ports
                        <span className="topology-selection-ports-count">{ports.length}</span>
                    </div>
                    {ports.length > 0 ? (
                        <div className="topology-selection-port-list">
                            {ports.map(port => (
                                <span
                                    key={port.id || `${port.port}-${port.protocol}`}
                                    className={`topology-selection-port topology-selection-port-${port.state || "open"}`}
                                    title={formatPortLabel(port)}
                                >
                                    {formatPortLabel(port)}
                                </span>
                            ))}
                        </div>
                    ) : (
                        <div className="topology-selection-empty">no ports in current view</div>
                    )}
                </div>
            ) : null}
        </div>
    );
}

function clamp(value, min, max)
{
    return Math.min(Math.max(value, min), max);
}

function hashEdgeId(id)
{
    let hash = 0;
    for (let index = 0; index < id.length; index += 1)
        hash = ((hash * 31) + id.charCodeAt(index)) % 9973;
    return hash;
}

function buildCurvedEdgePath(sourceX, sourceY, targetX, targetY, id)
{
    const dx = targetX - sourceX;
    const dy = Math.max(targetY - sourceY, 40);
    const hash = hashEdgeId(id);
    const direction = hash % 2 === 0 ? 1 : -1;
    const lateralBias = direction * (10 + (hash % 3) * 6);
    const bend = clamp((dx * 0.18) + lateralBias, -42, 42);
    const controlY = Math.max(dy * 0.34, 26);

    return [
        `M ${sourceX},${sourceY}`,
        `C ${sourceX + bend},${sourceY + controlY}`,
        `${targetX - bend},${targetY - controlY}`,
        `${targetX},${targetY}`
    ].join(" ");
}

function TopologyEdge({
    id,
    sourceX,
    sourceY,
    targetX,
    targetY,
    markerEnd,
    style,
    className
}) {
    const path = buildCurvedEdgePath(sourceX, sourceY, targetX, targetY, id);

    return (
        <BaseEdge
            id={id}
            path={path}
            markerEnd={markerEnd}
            style={style}
            className={className}
            interactionWidth={18}
        />
    );
}

const EDGE_TYPES = {
    topology: TopologyEdge
};

function presenceDotClass(presenceStatus)
{
    if (presenceStatus === "up")
        return "topo-node-presence-dot topo-node-presence-dot-up";
    if (presenceStatus === "paused")
        return "topo-node-presence-dot topo-node-presence-dot-paused";
    if (presenceStatus === "down" || presenceStatus === "timeout")
        return "topo-node-presence-dot topo-node-presence-dot-down";
    return "topo-node-presence-dot topo-node-presence-dot-unknown";
}

function PresenceDot({ presenceStatus })
{
    if (!presenceStatus)
        return null;

    const title = presenceStatus === "up"
        ? "presence: online"
        : presenceStatus === "paused"
        ? "presence: paused"
        : presenceStatus === "down" || presenceStatus === "timeout"
        ? "presence: offline"
        : `presence: ${presenceStatus}`;

    return (
        <span
            className={presenceDotClass(presenceStatus)}
            title={title}
            aria-label={title}
        />
    );
}

function TopologyNodeLabel({ node, isSelected, nodeState, isGateway, isCluster, presenceStatus })
{
    const nodeStateClass = nodeState ? ` topo-node-state-${nodeState}` : "";
    const presenceClass = presenceStatus === "up"
        ? " topo-node-presence-up"
        : presenceStatus === "paused"
        ? " topo-node-presence-paused"
        : (presenceStatus === "down" || presenceStatus === "timeout" || presenceStatus === "error")
        ? " topo-node-presence-down"
        : "";

    return (
        <div className={`topo-node topo-node-${node.kind}${nodeStateClass}${presenceClass}${isSelected ? " is-selected" : ""}`}>
            <div className="topo-node-head">
                <div className="topo-node-marker-wrap">
                    <div className="topo-node-marker">
                        <span className="topo-node-marker-core" />
                        {isCluster ? <span className="topo-node-cluster-count">{node.hostIds?.length || ""}</span> : null}
                    </div>
                </div>
                <div className="topo-node-header-text">
                    <div className="topo-node-type">{node.eyebrow}</div>
                    <div className="topo-node-primary">{node.title}</div>
                    {node.subtitle ? (
                        <div className="topo-node-secondary">{node.subtitle}</div>
                    ) : null}
                </div>
            </div>
            <div className="topo-node-body">
                {isGateway && node.detail ? (
                    <div className="topo-node-meta">{node.detail}</div>
                ) : null}
                {isCluster ? (
                    <div className={`topo-node-meta topo-node-cluster-hint${isSelected ? " is-selected" : ""}`}>
                        click to expand
                    </div>
                ) : null}
                {!isGateway && !isCluster ? <HostTagBadges meta={node._meta} /> : null}
                <div className="topo-node-footer">
                    <HostStatusBadge state={nodeState} />
                    <OpenPortCount node={node} state={nodeState} />
                    <PresenceDot presenceStatus={presenceStatus} />
                </div>
            </div>
        </div>
    );
}

function buildCanvasNodeStyle(isGateway, hasTags)
{
    const baseHostHeight = 72;
    return {
        border: "none",
        padding: 0,
        background: "transparent",
        width: isGateway ? 124 : HOST_NODE_WIDTH,
        height: isGateway ? 76 : (hasTags ? baseHostHeight + 16 : baseHostHeight),
        cursor: "pointer"
    };
}

function getPresenceStatus(node, presenceStatusByIp)
{
    if (!presenceStatusByIp || node.kind !== "host")
        return null;

    return presenceStatusByIp.get(getHostIp(node)) || null;
}

function buildCanvasNode(node, isSelected, hostStateByIp, presenceStatusByIp)
{
    const isGateway = node.kind === "gateway";
    const isCluster = node.kind === "cluster";
    const nodeState = getNodeState(node, hostStateByIp);
    const presenceStatus = getPresenceStatus(node, presenceStatusByIp);
    const hasTags = !isGateway && !isCluster
        && parseTags((node._meta && node._meta.tags) || "").length > 0;

    return {
        id: node.id,
        position: node.position,
        draggable: false,
        selectable: true,
        data: {
            _kind: node.kind,
            label: (
                <TopologyNodeLabel
                    node={node}
                    isSelected={isSelected}
                    nodeState={nodeState}
                    isGateway={isGateway}
                    isCluster={isCluster}
                    presenceStatus={presenceStatus}
                />
            )
        },
        style: buildCanvasNodeStyle(isGateway, hasTags)
    };
}

function buildCanvasEdge(edge, isHighlighted, isHovered)
{
    const active = isHighlighted || isHovered;

    return {
        ...edge,
        animated: false,
        selectable: false,
        type: "topology",
        className: isHovered ? "topo-edge-hovered" : undefined,
        markerEnd: EDGE_MARKER,
        style: {
            strokeWidth: active ? 1.35 : 0.95,
            opacity: active ? 0.88 : 0.48
        }
    };
}

// Returns { nodes, edges } with clusters expanded/collapsed based on collapsedClusters set.
// A cluster is shown expanded unless its ID is in collapsedClusters (default: all expanded).
// Positions are recomputed for expanded host nodes using a simple row-append approach.
function buildExpandedNodes({ graphNodes, clusterHostMap, allHostNodes, collapsedClusters })
{
    const hostLookup = new Map((allHostNodes || []).map(h => [h.id, h]));
    const resultNodes = [];
    let maxY = 0;
    graphNodes.forEach(n => { if (n.position && n.position.y > maxY) maxY = n.position.y; });
    const expandedRowY = maxY + 120;

    graphNodes.forEach(node => {
        if (node.kind === "cluster" && !collapsedClusters.has(node.id)) {
            // Replace cluster with individual host nodes.
            const hostDisplayNodes = clusterHostMap ? clusterHostMap.get(node.id) : null;
            if (!hostDisplayNodes || hostDisplayNodes.length === 0) {
                resultNodes.push(node);
                return;
            }
            const total = hostDisplayNodes.length;
            const nodeWidth = EXPANDED_NODE_WIDTH;
            const nodeSep = EXPANDED_NODE_GAP;
            const totalWidth = total * nodeWidth + Math.max(total - 1, 0) * nodeSep;
            const startX = node.position.x - totalWidth / 2 + nodeWidth / 2;
            hostDisplayNodes.forEach((hn, index) => {
                const expanded = hostLookup.get(hn.id) || hn;
                resultNodes.push({
                    ...expanded,
                    position: { x: startX + index * (nodeWidth + nodeSep), y: expandedRowY }
                });
            });
        } else {
            resultNodes.push(node);
        }
    });

    return resultNodes;
}

function buildExpandedEdges({ graphEdges, resultNodes, clusterHostMap, collapsedClusters })
{
    const resultEdges = [];
    const resultNodeLookup = new Map(resultNodes.map(node => [node.id, node]));
    const seenEdgeKeys = new Set();

    graphEdges.forEach(edge => {
        const targetNode = resultNodeLookup.get(edge.target);
        const sourceNode = resultNodeLookup.get(edge.source);

        // If the target was a cluster that's now expanded, replace with per-host edges.
        if (!targetNode && !collapsedClusters.has(edge.target)) {
            const hostDisplayNodes = clusterHostMap ? clusterHostMap.get(edge.target) : null;
            if (hostDisplayNodes) {
                hostDisplayNodes.forEach(hn => {
                    const key = `${edge.source}->${hn.id}`;
                    if (!seenEdgeKeys.has(key)) {
                        seenEdgeKeys.add(key);
                        resultEdges.push({ id: key, source: edge.source, target: hn.id });
                    }
                });
            }
            return;
        }

        // If source was a cluster that's expanded, skip (shouldn't occur in 2-level model).
        if (!sourceNode && !collapsedClusters.has(edge.source))
            return;

        const key = `${edge.source}->${edge.target}`;
        if (!seenEdgeKeys.has(key)) {
            seenEdgeKeys.add(key);
            resultEdges.push(edge);
        }
    });

    return resultEdges;
}

function applyExpansion(graphNodes, graphEdges, clusterHostMap, allHostNodes, collapsedClusters)
{
    const resultNodes = buildExpandedNodes({
        graphNodes,
        clusterHostMap,
        allHostNodes,
        collapsedClusters
    });
    const resultEdges = buildExpandedEdges({ graphEdges, resultNodes, clusterHostMap, collapsedClusters });

    return { nodes: resultNodes, edges: resultEdges };
}

function useTopologyGraphState(graphNodes, graphEdges, clusterHostMap, allHostNodes, collapsedClusters)
{
    const { nodes: activeNodes, edges: activeEdges } = useMemo(
        () => applyExpansion(
            Array.isArray(graphNodes) ? graphNodes : [],
            Array.isArray(graphEdges) ? graphEdges : [],
            clusterHostMap,
            allHostNodes,
            collapsedClusters
        ),
        [graphNodes, graphEdges, clusterHostMap, allHostNodes, collapsedClusters]
    );

    return { activeNodes, activeEdges };
}

function useTopologyInteractions(setCollapsedClusters, setSelectedNodeId, setTooltip, tooltipNodeId)
{
    const handleNodeClick = useCallback((_, node) => {
        if (node.data && node.data._kind === "cluster") {
            setCollapsedClusters(prev => {
                const next = new Set(prev);
                if (next.has(node.id))
                    next.delete(node.id);
                else
                    next.add(node.id);
                return next;
            });
            setSelectedNodeId(null);
            return;
        }
        setSelectedNodeId(previous => previous === node.id ? null : node.id);
    }, [setCollapsedClusters, setSelectedNodeId]);

    const handleNodeMouseEnter = useCallback((event, node) => {
        tooltipNodeId.current = node.id;
        setTooltip({ nodeId: node.id, x: event.clientX, y: event.clientY });
    }, [setTooltip, tooltipNodeId]);

    const handleNodeMouseMove = useCallback((event, node) => {
        if (tooltipNodeId.current !== node.id)
            return;
        setTooltip({ nodeId: node.id, x: event.clientX, y: event.clientY });
    }, [setTooltip, tooltipNodeId]);

    const handleNodeMouseLeave = useCallback(() => {
        tooltipNodeId.current = null;
        setTooltip(null);
    }, [setTooltip, tooltipNodeId]);

    return { handleNodeClick, handleNodeMouseEnter, handleNodeMouseMove, handleNodeMouseLeave };
}

function buildLegendState(activeNodes, hostStateByIp, presenceStatusByIp)
{
    const graphNodes = Array.isArray(activeNodes) ? activeNodes : [];
    const hostStates = hostStateByIp instanceof Map ? Array.from(hostStateByIp.values()) : [];
    const presenceStates = presenceStatusByIp instanceof Map ? Array.from(presenceStatusByIp.values()) : [];

    return {
        hasGateway: graphNodes.some(node => node.kind === "gateway"),
        hasHosts: graphNodes.some(node => node.kind === "host"),
        hasClusters: graphNodes.some(node => node.kind === "cluster"),
        hasNewHosts: hostStates.includes("new"),
        hasGoneHosts: hostStates.includes("gone"),
        hasTrackedUp: presenceStates.includes("up"),
        hasTrackedPaused: presenceStates.includes("paused"),
        hasTrackedDown: presenceStates.some(s => s === "down" || s === "timeout")
    };
}

function buildViewportSignature(activeNodes, activeEdges, expandedClusters)
{
    const nodeIds = (Array.isArray(activeNodes) ? activeNodes : []).map(node => node.id).join("|");
    const edgeIds = (Array.isArray(activeEdges) ? activeEdges : []).map(edge => edge.id).join("|");
    const clusterIds = expandedClusters instanceof Set ? Array.from(expandedClusters).sort().join("|") : "";
    return `${nodeIds}::${edgeIds}::${clusterIds}`;
}

function AutoFitController({ fitRequestKey })
{
    const { fitView } = useReactFlow();
    const nodesInitialized = useNodesInitialized();
    const lastAppliedKeyRef = useRef("");

    useEffect(() => {
        if (!nodesInitialized || !fitRequestKey)
            return undefined;

        if (fitRequestKey === lastAppliedKeyRef.current)
            return undefined;

        lastAppliedKeyRef.current = fitRequestKey;
        let cancelled = false;

        const frameId = requestAnimationFrame(() => {
            if (cancelled)
                return;

            fitView({ padding: FIT_PADDING, minZoom: FIT_MIN_ZOOM, maxZoom: 0.88, duration: 200 });
        });

        return () => {
            cancelled = true;
            cancelAnimationFrame(frameId);
        };
    }, [fitRequestKey, fitView, nodesInitialized]);

    return null;
}

function TopologyCanvasFlow({
    nodes,
    edges,
    legendState,
    fitRequestKey,
    setSelectedNodeId,
    setHoveredEdgeId,
    handleNodeClick,
    handleNodeMouseEnter,
    handleNodeMouseMove,
    handleNodeMouseLeave,
    onResetTopology
})
{
    return (
        <ReactFlow
            nodes={nodes}
            edges={edges}
            edgeTypes={EDGE_TYPES}
            minZoom={FIT_MIN_ZOOM}
            maxZoom={1.4}
            nodesConnectable={false}
            nodesDraggable={false}
            elementsSelectable
            panOnDrag
            zoomOnScroll
            zoomOnPinch
            zoomOnDoubleClick={false}
            preventScrolling={false}
            onNodeClick={handleNodeClick}
            onPaneClick={() => setSelectedNodeId(null)}
            onEdgeMouseEnter={(_, edge) => setHoveredEdgeId(edge.id)}
            onEdgeMouseLeave={() => setHoveredEdgeId(null)}
            onNodeMouseEnter={handleNodeMouseEnter}
            onNodeMouseMove={handleNodeMouseMove}
            onNodeMouseLeave={handleNodeMouseLeave}
            proOptions={{ hideAttribution: true }}
        >
            <AutoFitController fitRequestKey={fitRequestKey} />
            <Background gap={20} size={1} />
            <TopologyLegend flags={legendState} />
            <TopoHud onResetTopology={onResetTopology} />
        </ReactFlow>
    );
}

export function TopologyCanvas({
    nodes: graphNodes,
    edges: graphEdges,
    clusterHostMap,
    allHostNodes,
    hostStateByIp,
    presenceStatusByIp,
    onResetTopology
})
{
    const [selectedNodeId, setSelectedNodeId] = useState(null);
    const [hoveredEdgeId, setHoveredEdgeId] = useState(null);
    const [tooltip, setTooltip] = useState(null);
    const [collapsedClusters, setCollapsedClusters] = useState(new Set());
    const tooltipNodeId = useRef(null);

    const { activeNodes, activeEdges } = useTopologyGraphState(
        graphNodes,
        graphEdges,
        clusterHostMap,
        allHostNodes,
        collapsedClusters
    );

    const hoveredEdge = useMemo(
        () => activeEdges.find(edge => edge.id === hoveredEdgeId) || null,
        [activeEdges, hoveredEdgeId]
    );
    const selectedNode = useMemo(
        () => activeNodes.find(node => node.id === selectedNodeId) || null,
        [activeNodes, selectedNodeId]
    );
    const nodes = useMemo(
        () => activeNodes.map(node => {
            const isSelected = node.id === selectedNodeId;
            const isEdgeHighlighted = hoveredEdge != null
                && (node.id === hoveredEdge.source || node.id === hoveredEdge.target);
            return buildCanvasNode(node, isSelected || isEdgeHighlighted, hostStateByIp, presenceStatusByIp);
        }),
        [activeNodes, selectedNodeId, hoveredEdge, hostStateByIp, presenceStatusByIp]
    );
    const edges = useMemo(
        () => activeEdges.map(edge => buildCanvasEdge(
            edge,
            selectedNodeId != null && (edge.source === selectedNodeId || edge.target === selectedNodeId),
            edge.id === hoveredEdgeId
        )),
        [activeEdges, selectedNodeId, hoveredEdgeId]
    );

    const tooltipGraphNode = useMemo(
        () => tooltip
            ? activeNodes.find(n => n.id === tooltip.nodeId) || null
            : null,
        [activeNodes, tooltip]
    );
    const legendState = useMemo(
        () => buildLegendState(activeNodes, hostStateByIp, presenceStatusByIp),
        [activeNodes, hostStateByIp, presenceStatusByIp]
    );
    const viewportSignature = useMemo(
        () => buildViewportSignature(activeNodes, activeEdges, collapsedClusters),
        [activeNodes, activeEdges, collapsedClusters]
    );

    const {
        handleNodeClick,
        handleNodeMouseEnter,
        handleNodeMouseMove,
        handleNodeMouseLeave
    } = useTopologyInteractions(setCollapsedClusters, setSelectedNodeId, setTooltip, tooltipNodeId);

    return (
        <div className="topology-canvas-shell">
            <div className="topology-canvas-hint-bar">
                drag to pan · scroll to zoom · click node to inspect · click subnet to expand
            </div>
            {selectedNode ? (
                <div className="topology-selection-overlay">
                    <SelectedNodeDetails node={selectedNode} />
                </div>
            ) : null}
            <TopologyCanvasFlow
                nodes={nodes}
                edges={edges}
                legendState={legendState}
                fitRequestKey={viewportSignature}
                setSelectedNodeId={setSelectedNodeId}
                setHoveredEdgeId={setHoveredEdgeId}
                handleNodeClick={handleNodeClick}
                handleNodeMouseEnter={handleNodeMouseEnter}
                handleNodeMouseMove={handleNodeMouseMove}
                handleNodeMouseLeave={handleNodeMouseLeave}
                onResetTopology={onResetTopology}
            />
            <NodeTooltip node={tooltipGraphNode} x={tooltip ? tooltip.x : 0} y={tooltip ? tooltip.y : 0} />
        </div>
    );
}
