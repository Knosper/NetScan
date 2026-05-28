import React, { useEffect, useMemo, useState } from "react";
import { PageShell, PageSection, ListRegion } from "../components/PageShell.jsx";
import { Button, Chip, Icon, PortRiskBadge } from "../atoms.jsx";
import { getHostLabel, getHostMeta } from "../ui/helpers.js";
import { InlineBanner, StateNotice } from "../ui/notices.jsx";
import { HostMetaBadges } from "../components/HostList.jsx";
import { TopologyCanvas } from "../topology/TopologyCanvas.jsx";
import { mergeTopologyPayloads, normalizeTopologyGraph } from "../topology/graphModel.js";
import { useScanContext } from "../ScanContext.jsx";
import { useHostContext } from "../HostContext.jsx";
import { usePresenceContext } from "../PresenceContext.jsx";

function addChangedHost(byIp, host, changeInfo) {
    if (!host?.ip)
        return;

    byIp[host.ip] = {
        ip: host.ip,
        name: host.name || "",
        status: changeInfo.status,
        acknowledged: Boolean(host.acknowledged),
        acknowledgement: { category: changeInfo.category, ip: host.ip },
        ports: []
    };
}

function addChangedPortEntries(byIp, entries, changeInfo) {
    entries.forEach(entry => {
        if (!entry?.ip) return;
        if (!byIp[entry.ip])
            byIp[entry.ip] = { ip: entry.ip, name: entry.name || "", status: "changed", ports: [] };
        byIp[entry.ip].ports.push({
            port: entry.port,
            service: entry.service || "",
            name: entry.name || "",
            change: changeInfo.change,
            acknowledged: Boolean(entry.acknowledged),
            acknowledgement: { category: changeInfo.category, ip: entry.ip, port: entry.port }
        });
    });
}

function extractChangedHosts(diffPayload) {
    if (!diffPayload)
        return [];

    const byIp = {};

    const newHosts = Array.isArray(diffPayload.newHosts) ? diffPayload.newHosts : [];
    const disappearedHosts = Array.isArray(diffPayload.disappearedHosts) ? diffPayload.disappearedHosts : [];
    const newOpenPorts = Array.isArray(diffPayload.newOpenPorts) ? diffPayload.newOpenPorts : [];
    const disappearedPorts = Array.isArray(diffPayload.disappearedPorts) ? diffPayload.disappearedPorts : [];

    newHosts.forEach(host => addChangedHost(byIp, host, {status: "new", category: "new_host"}));
    disappearedHosts.forEach(host => addChangedHost(byIp, host, {status: "gone", category: "disappeared_host"}));
    addChangedPortEntries(byIp, newOpenPorts, {change: "new", category: "new_open_port"});
    addChangedPortEntries(byIp, disappearedPorts, {change: "gone", category: "disappeared_port"});

    return Object.values(byIp);
}

function buildSummary(changeSummary) {
    const newHosts = Array.isArray(changeSummary?.newHosts) ? changeSummary.newHosts : [];
    const disappearedHosts = Array.isArray(changeSummary?.disappearedHosts)
        ? changeSummary.disappearedHosts
        : [];
    const newOpenPorts = Array.isArray(changeSummary?.newOpenPorts) ? changeSummary.newOpenPorts : [];
    const disappearedPorts = Array.isArray(changeSummary?.disappearedPorts)
        ? changeSummary.disappearedPorts
        : [];

    const allEntries = [...newHosts, ...disappearedHosts, ...newOpenPorts, ...disappearedPorts];
    let acknowledged = 0;
    let unresolved = 0;
    for (const entry of allEntries) {
        if (entry?.acknowledged) acknowledged++;
        else unresolved++;
    }

    return {
        addedHosts: Number(changeSummary?.addedHosts ?? newHosts.length ?? 0),
        removedHosts: Number(changeSummary?.removedHosts ?? disappearedHosts.length ?? 0),
        addedPorts: Number(changeSummary?.addedPorts ?? newOpenPorts.length ?? 0),
        removedPorts: Number(changeSummary?.removedPorts ?? disappearedPorts.length ?? 0),
        acknowledged,
        unresolved,
        newHosts,
        disappearedHosts,
        newOpenPorts,
        disappearedPorts
    };
}

function buildSummaryChips(summary, hasBaseline) {
    if (!hasBaseline)
        return [];

    const chips = [];

    if (summary.addedHosts > 0)
        chips.push({ key: "added-hosts", tone: "violet", text: `+${summary.addedHosts} hosts` });
    if (summary.removedHosts > 0)
        chips.push({ key: "removed-hosts", tone: "danger", text: `-${summary.removedHosts} hosts` });
    if (summary.addedPorts > 0)
        chips.push({ key: "added-ports", tone: "violet", text: `+${summary.addedPorts} ports` });
    if (summary.removedPorts > 0)
        chips.push({ key: "removed-ports", tone: "danger", text: `-${summary.removedPorts} ports` });
    if (summary.unresolved > 0)
        chips.push({ key: "unresolved", tone: "info", text: `${summary.unresolved} unresolved` });
    if (summary.acknowledged > 0)
        chips.push({ key: "acknowledged", tone: "muted", text: `${summary.acknowledged} acknowledged` });

    return chips;
}

function buildPresenceStatusByIp(trackers) {
    const statusMap = new Map();

    (Array.isArray(trackers) ? trackers : []).forEach(tracker => {
        if (!tracker?.target)
            return;

        const status = tracker.enabled === false ? "paused" : (tracker.lastResult?.status || "unknown");
        statusMap.set(tracker.target, status);
    });

    return statusMap;
}

function buildTopologyHostStateMap(changedHosts) {
    const hostStates = new Map();

    (Array.isArray(changedHosts) ? changedHosts : []).forEach(host => {
        if (!host?.ip)
            return;

        hostStates.set(host.ip, host.status === "gone" || host.status === "new" ? host.status : "changed");
    });

    return hostStates;
}

function getHostOpenPortCount(host) {
    const value = Number(host?.openPortCount ?? host?.open_port_count ?? 0);
    return Number.isFinite(value) && value > 0 ? value : 0;
}

function addTopologyHost(hostMap, host, openPortCount = 0) {
    if (!host?.ip)
        return;

    const existing = hostMap.get(host.ip) || {};
    hostMap.set(host.ip, {
        ...existing,
        ...host,
        openPortCount: Math.max(
            getHostOpenPortCount(existing),
            getHostOpenPortCount(host),
            openPortCount
        )
    });
}

function buildHostTopologyPayload(dashboardHosts, dashboardPorts, fallbackHosts) {
    const hostMap = new Map();

    (Array.isArray(dashboardHosts) ? dashboardHosts : []).forEach(host => addTopologyHost(hostMap, host));
    (Array.isArray(fallbackHosts) ? fallbackHosts : [])
        .filter(host => getHostOpenPortCount(host) > 0)
        .forEach(host => addTopologyHost(hostMap, host));

    const ports = Array.isArray(dashboardPorts) ? dashboardPorts : [];
    const portCountsByHost = new Map();
    ports.forEach(port => {
        if (!port?.host)
            return;
        portCountsByHost.set(port.host, (portCountsByHost.get(port.host) || 0) + 1);
    });
    portCountsByHost.forEach((openPortCount, ip) => {
        addTopologyHost(hostMap, { ip, name: "" }, openPortCount);
    });

    const visibleHosts = Array.from(hostMap.values())
        .filter(host => host?.ip && getHostOpenPortCount(host) > 0);

    if (!visibleHosts.length)
        return null;

    const nodes = [{
        id: "gw",
        type: "gateway",
        label: "NetScan",
        meta: {
            host_discovery_only: "false",
            scan_target: "current database state"
        }
    }];
    const edges = [];

    const hostIds = new Set();
    visibleHosts.forEach(host => {
        const hostId = `host::${host.ip}`;
        hostIds.add(hostId);
        nodes.push({
            id: hostId,
            type: "host",
            label: host.name || host.ip,
            meta: {
                ip: host.ip,
                name: host.name || host.meta?.displayName || "",
                openPortCount: String(getHostOpenPortCount(host))
            }
        });
        edges.push({
            id: `gw--${hostId}`,
            source: "gw",
            target: hostId
        });
    });

    ports.forEach(port => {
        if (!port?.host)
            return;
        const hostId = `host::${port.host}`;
        if (!hostIds.has(hostId))
            return;

        const portId = `port::${port.host}::${port.port || "?"}/tcp`;
        nodes.push({
            id: portId,
            type: "port",
            label: `${port.port || "?"}/tcp${port.service ? ` ${port.service}` : ""}`,
            meta: {
                ip: port.host,
                port: String(port.port || ""),
                protocol: "tcp",
                service: port.service || "",
                state: "open"
            }
        });
        edges.push({
            id: `${hostId}--${portId}`,
            source: hostId,
            target: portId
        });
    });

    return { nodes, edges };
}

function formatPortDiffEntry(entry) {
    const ip = entry?.ip || "unknown-host";
    const portValue = Number(entry?.port ?? 0);
    const port = Number.isFinite(portValue) && portValue > 0 ? `${portValue}` : "?";
    const service = entry?.service ? `/${entry.service}` : "";
    const hostName = entry?.name ? ` (${entry.name})` : "";
    return `${ip}:${port}${service}${hostName}`;
}

function renderPortDetails(entries, change, maxEntries = 8) {
    if (!Array.isArray(entries) || entries.length === 0)
        return null;

    const visibleEntries = entries.slice(0, maxEntries);
    const remainingCount = entries.length - visibleEntries.length;

    return (
        <div className="changes-summary-detail-group">
            <div className="mono-label changes-summary-detail-title">
                {change === "gone" ? "closed ports" : "opened ports"}
            </div>
            <div className="changes-summary-detail-list">
                {visibleEntries.map((entry, index) => (
                    <div
                        key={`${change}-${entry?.ip || "unknown"}-${entry?.port || "?"}-${index}`}
                        className={`changes-summary-detail-row ${change} ${entry?.acknowledged ? "acknowledged" : ""}`}
                    >
                        <span className="changes-summary-detail-marker">{change === "gone" ? "-" : "+"}</span>
                        <span className="changes-summary-detail-text">{formatPortDiffEntry(entry)}</span>
                        {entry?.acknowledged ? <Chip tone="muted">acknowledged</Chip> : null}
                    </div>
                ))}
                {remainingCount > 0 ? (
                    <div className="changes-summary-detail-more">+{remainingCount} more</div>
                ) : null}
            </div>
        </div>
    );
}

function HostPortRow({ p, onAcknowledge }) {
    return (
        <div className={`changes-port-row ${p.change} ${p.acknowledged ? "acknowledged" : ""}`}>
            <span className="changes-port-marker">{p.change === "new" ? "+" : "-"}</span>
            <span className="changes-port-num">{p.port}</span>
            <PortRiskBadge port={p.port} />
            {p.service ? <span className="changes-port-service">{p.service}</span> : null}
            {p.name ? <span className="changes-port-name">{p.name}</span> : null}
            {p.acknowledged ? <Chip tone="muted">acknowledged</Chip> : null}
            <Button
                type="button"
                variant="ghost"
                size="sm"
                className="changes-port-ack-button"
                onClick={() => onAcknowledge(p.acknowledgement, !p.acknowledged)}
            >
                {p.acknowledged ? "reopen" : "acknowledge"}
            </Button>
        </div>
    );
}

const HOST_STATUS_META = {
    new: { tone: "violet", label: "new host" },
    gone: { tone: "danger", label: "disappeared" }
};
const HOST_STATUS_META_DEFAULT = { tone: "muted", label: "changed" };

function HostEntry({ host, isOpen, onToggle, onAcknowledge, hosts }) {
    const { tone: statusTone, label: statusLabel } = HOST_STATUS_META[host.status] || HOST_STATUS_META_DEFAULT;
    const canAcknowledgeHost = host.status === "new" || host.status === "gone";
    const label = getHostLabel(host.ip, hosts);
    const hostMeta = getHostMeta(host.ip, hosts);

    return (
        <div className={`changes-host-entry ${host.acknowledged ? "acknowledged" : ""}`}>
            <button
                type="button"
                className="changes-host-header"
                onClick={onToggle}
                aria-expanded={isOpen}
            >
                <span className="changes-host-chevron">{Icon.chevron({ dir: isOpen ? "down" : "right" })}</span>
                <span className="changes-host-ip">{label}</span>
                {host.name ? <span className="changes-host-name">{host.name}</span> : null}
                <HostMetaBadges meta={hostMeta} />
                <Chip tone={statusTone}>{statusLabel}</Chip>
                {host.acknowledged ? <Chip tone="muted">acknowledged</Chip> : null}
            </button>
            {canAcknowledgeHost ? (
                <div className="changes-ack-action">
                    <Button
                        type="button"
                        variant="ghost"
                        size="sm"
                        onClick={() => onAcknowledge(host.acknowledgement, !host.acknowledged)}
                    >
                        {host.acknowledged ? "reopen" : "acknowledge"}
                    </Button>
                </div>
            ) : null}
            {isOpen && host.ports.length > 0 ? (
                <div className="changes-host-ports">
                    {host.ports.map(p => (
                        <HostPortRow key={`${p.change}-${p.port}`} p={p} onAcknowledge={onAcknowledge} />
                    ))}
                </div>
            ) : null}
        </div>
    );
}

function buildTopologyStateForError(activeTopology, backendUnavailable, offlineMessage) {
    if (activeTopology.kind === "not-ready")
        return { kind: "not-ready", message: activeTopology.message || "topology not ready" };

    if (activeTopology.kind === "not-found")
        return { kind: "not-found", message: activeTopology.message || "topology unavailable" };

    if (activeTopology.kind === "error")
        return {
            kind: backendUnavailable ? "offline" : "failed",
            message: backendUnavailable ? offlineMessage : (activeTopology.message || "topology failed")
        };

    return { kind: "failed", message: "topology state unavailable" };
}

function isCompletedScan(scan) {
    return (scan?.state || scan?.status) === "completed";
}

function collectCompletedScanIds(scans) {
    return (Array.isArray(scans) ? scans : [])
        .filter(isCompletedScan)
        .map(scan => Number(scan?.id || 0))
        .filter(scanId => scanId > 0);
}

function buildTopologyState({
    activeScanId,
    activeTopology,
    activeTopologyGraph,
    backendUnavailable,
    offlineMessage
}) {
    if (!activeScanId)
        return { kind: "stale", message: "no completed scan available for topology" };

    if (activeTopologyGraph && activeTopologyGraph.nodes.length > 0)
        return { kind: "ready", graph: activeTopologyGraph };

    if (!activeTopology || activeTopology.kind === "loading")
        return { kind: "loading", message: `loading topology for scan #${activeScanId}` };

    if (activeTopology.kind === "ready" && activeTopologyGraph) {
        if (!activeTopologyGraph.nodes.length)
            return { kind: "empty", message: "completed scan has no topology hosts with open ports" };

        return { kind: "ready", graph: activeTopologyGraph };
    }

    return buildTopologyStateForError(activeTopology, backendUnavailable, offlineMessage);
}

function buildDiffStateForReady(activeDiff, hasBaseline, hasChanges) {
    if (!hasBaseline)
        return { kind: "stale", message: "No baseline available yet. A baseline is created from the first completed scan for each target. Run a second scan against the same target to see what changed." };

    if (!hasChanges)
        return { kind: "completed", message: "no changes detected against baseline" };

    return { kind: "changed", message: "diff stream ready for analysis" };
}

function buildDiffState({
    activeScanId,
    activeDiff,
    hasBaseline,
    hasChanges,
    backendUnavailable,
    offlineMessage
}) {
    if (!activeScanId)
        return { kind: "stale", message: "no completed scan available for diff" };

    if (!activeDiff || activeDiff.kind === "loading")
        return { kind: "loading", message: `loading diff for scan #${activeScanId}` };

    if (activeDiff.kind === "not-found")
        return { kind: "not-found", message: activeDiff.message || "comparison unavailable" };

    if (activeDiff.kind === "not-diffable")
        return { kind: "stale", message: activeDiff.message || "comparison not available for this scan" };

    if (activeDiff.kind === "error")
        return {
            kind: backendUnavailable ? "offline" : "failed",
            message: backendUnavailable ? offlineMessage : (activeDiff.message || "comparison failed")
        };

    if (activeDiff.kind === "ready")
        return buildDiffStateForReady(activeDiff, hasBaseline, hasChanges);

    return { kind: "idle", message: "diff state unavailable" };
}

function ChangeSummarySection({ activeDiff, changes, summaryChips, summary, summarySubtitle, drilldownFromScan, selectedScanId }) {
    if (activeDiff?.kind !== "ready")
        return null;

    const hasBaseline = Boolean(changes?.hasBaseline);

    return (
        <PageSection title="change summary" sub="diff output is separate from topology rendering" flush>
            <div className="changes-summary-panel">
                <div className="changes-summary-head">
                    <div className="changes-summary-meta">
                        <div className="mono-label changes-summary-title">change summary</div>
                        <div className="changes-summary-sub">{summarySubtitle}</div>
                    </div>
                    {drilldownFromScan && selectedScanId ? (
                        <Chip tone="info">scan #{selectedScanId}</Chip>
                    ) : null}
                </div>
                {summaryChips.length > 0 ? (
                    <div className="changes-summary-chips">
                        {summaryChips.map(chip => (
                            <Chip key={chip.key} tone={chip.tone}>{chip.text}</Chip>
                        ))}
                    </div>
                ) : null}
                {hasBaseline ? (
                    <div className="changes-summary-details">
                        {renderPortDetails(summary.disappearedPorts, "gone")}
                        {renderPortDetails(summary.newOpenPorts, "new")}
                    </div>
                ) : null}
            </div>
        </PageSection>
    );
}

function TopologySection({
    topologyState,
    topologyHostStateMap,
    presenceStatusByIp,
    mergedView
}) {
    return (
        <PageSection
            title="topology"
            sub={mergedView ? "merged from completed scans" : "rendered from the selected completed scan"}
            flush
        >
            <ListRegion className="changes-topology-region">
                {topologyState.kind === "ready" ? (
                    <TopologyCanvas
                        nodes={topologyState.graph.nodes}
                        edges={topologyState.graph.edges}
                        clusterHostMap={topologyState.graph.clusterHostMap}
                        allHostNodes={topologyState.graph.allHostNodes}
                        hostStateByIp={topologyHostStateMap}
                        presenceStatusByIp={presenceStatusByIp}
                    />
                ) : (
                    <StateNotice kind={topologyState.kind} message={topologyState.message} compact />
                )}
            </ListRegion>
        </PageSection>
    );
}

export function ChangesScreen({
    backendUnavailable,
    offlineMessage,
    banners,
    dashboard,
    screenExtra,
    localIp
}) {
    const {
        scanDiffs,
        scanTopologies,
        loadScanDiff,
        updateDiffAcknowledgement,
        loadScanTopology,
        loadScanTopologies
    } = useScanContext();
    const { hosts } = useHostContext();
    const { presenceTrackers } = usePresenceContext();
    const selectedScanId = Number(screenExtra?.scanId || 0) || null;
    const drilldownFromScan = screenExtra?.source === "scans" && selectedScanId !== null;
    const selectedScanDiff = selectedScanId !== null ? scanDiffs?.[selectedScanId] : null;
    const selectedScanTopology = selectedScanId !== null ? scanTopologies?.[selectedScanId] : null;
    const completedScanIds = useMemo(() => collectCompletedScanIds(dashboard?.scans), [dashboard?.scans]);
    const latestCompletedScanId = completedScanIds[0] || null;
    const latestScanDiff = latestCompletedScanId !== null ? scanDiffs?.[latestCompletedScanId] : null;
    const mergedTopologyEntries = useMemo(
        () => completedScanIds.map(scanId => ({ scanId, topology: scanTopologies?.[scanId] || null })),
        [completedScanIds, scanTopologies]
    );
    const mergedReadyTopologyPayload = useMemo(() => {
        const readyPayloads = mergedTopologyEntries
            .map(entry => entry.topology?.kind === "ready" ? entry.topology.payload : null)
            .filter(Boolean);

        if (!readyPayloads.length)
            return null;

        return mergeTopologyPayloads(readyPayloads);
    }, [mergedTopologyEntries]);
    const hostTopologyPayload = useMemo(
        () => buildHostTopologyPayload(dashboard?.hosts, dashboard?.ports, hosts),
        [dashboard?.hosts, dashboard?.ports, hosts]
    );
    const mergedDisplayTopologyPayload = useMemo(() => {
        const payloads = [mergedReadyTopologyPayload, hostTopologyPayload].filter(Boolean);
        if (!payloads.length)
            return null;
        return mergeTopologyPayloads(payloads);
    }, [mergedReadyTopologyPayload, hostTopologyPayload]);

    const [expandedIp, setExpandedIp] = useState(null);

    useEffect(() => {
        if (!drilldownFromScan || !selectedScanId || !loadScanDiff) return;
        if (!selectedScanDiff) loadScanDiff(selectedScanId);
    }, [drilldownFromScan, selectedScanId, selectedScanDiff, loadScanDiff]);

    useEffect(() => {
        if (!drilldownFromScan || !selectedScanId || !loadScanTopology) return;
        if (!selectedScanTopology) loadScanTopology(selectedScanId);
    }, [drilldownFromScan, selectedScanId, selectedScanTopology, loadScanTopology]);

    useEffect(() => {
        if (drilldownFromScan || !latestCompletedScanId || !loadScanDiff) return;
        if (!latestScanDiff) loadScanDiff(latestCompletedScanId);
    }, [drilldownFromScan, latestCompletedScanId, latestScanDiff, loadScanDiff]);

    useEffect(() => {
        if (drilldownFromScan || !completedScanIds.length || !loadScanTopologies)
            return;

        loadScanTopologies(completedScanIds);
    }, [drilldownFromScan, completedScanIds, loadScanTopologies]);

    const activeDiff = drilldownFromScan ? selectedScanDiff : latestScanDiff;
    const activeTopology = drilldownFromScan ? selectedScanTopology : null;
    const activeTopologyPayload = drilldownFromScan
        ? (activeTopology?.kind === "ready" ? activeTopology.payload : null)
        : mergedDisplayTopologyPayload;

    const activeTopologyGraph = useMemo(() => {
        if (!activeTopologyPayload) return null;
        return normalizeTopologyGraph(activeTopologyPayload, localIp);
    }, [activeTopologyPayload, localIp]);
    const hostTopologyGraph = useMemo(() => {
        if (!hostTopologyPayload) return null;
        return normalizeTopologyGraph(hostTopologyPayload, localIp);
    }, [hostTopologyPayload, localIp]);

    const mergedTopologyState = useMemo(() => {
        if (!completedScanIds.length)
            return { kind: "stale", message: "no completed scan available for topology" };

        if (activeTopologyGraph && activeTopologyGraph.nodes.length > 0)
            return { kind: "ready", graph: activeTopologyGraph };
        if (hostTopologyGraph && hostTopologyGraph.nodes.length > 0)
            return { kind: "ready", graph: hostTopologyGraph };

        const topologyStates = mergedTopologyEntries
            .map(entry => entry.topology)
            .filter(Boolean);

        if (!topologyStates.length || topologyStates.some(state => state.kind === "loading"))
            return { kind: "loading", message: "loading topology from completed scans" };

        if (topologyStates.some(state => state.kind === "error"))
            return {
                kind: backendUnavailable ? "offline" : "failed",
                message: backendUnavailable ? offlineMessage : "topology failed"
            };

        if (mergedDisplayTopologyPayload && activeTopologyGraph && !activeTopologyGraph.nodes.length)
            return { kind: "empty", message: "completed scans have no topology hosts with open ports" };

        return { kind: "stale", message: "no topology available from completed scans" };
    }, [completedScanIds, mergedTopologyEntries, mergedDisplayTopologyPayload, activeTopologyGraph,
        hostTopologyGraph, backendUnavailable, offlineMessage]);

    const topologyState = useMemo(() => buildTopologyState({
        activeScanId: selectedScanId,
        activeTopology,
        activeTopologyGraph,
        backendUnavailable,
        offlineMessage
    }), [selectedScanId, activeTopology, activeTopologyGraph, backendUnavailable, offlineMessage]);

    const effectiveTopologyState = drilldownFromScan ? topologyState : mergedTopologyState;

    const changes = drilldownFromScan
        ? (selectedScanDiff?.kind === "ready" ? selectedScanDiff.payload || {} : {})
        : (latestScanDiff?.kind === "ready"
            ? latestScanDiff.payload || {}
            : (dashboard?.changes || dashboard?.changeSummary || {}));

    const summary = buildSummary(changes);
    const hasBaseline = drilldownFromScan
        ? (selectedScanDiff?.kind === "ready" ? Boolean(changes?.hasBaseline) : false)
        : Boolean(changes?.hasBaseline);
    const summaryChips = buildSummaryChips(summary, hasBaseline);

    const diffHosts = useMemo(() => {
        if (!hasBaseline || activeDiff?.kind !== "ready") return [];
        return extractChangedHosts(changes);
    }, [changes, hasBaseline, activeDiff]);

    const topologyHostStateMap = useMemo(() => buildTopologyHostStateMap(diffHosts), [diffHosts]);
    const presenceStatusByIp = useMemo(() => buildPresenceStatusByIp(presenceTrackers), [presenceTrackers]);
    const hasChanges = diffHosts.length > 0;

    const diffState = useMemo(() => buildDiffState({
        activeScanId: drilldownFromScan ? selectedScanId : latestCompletedScanId,
        activeDiff,
        hasBaseline,
        hasChanges,
        backendUnavailable,
        offlineMessage
    }), [drilldownFromScan, selectedScanId, latestCompletedScanId, activeDiff, hasBaseline, hasChanges,
        backendUnavailable, offlineMessage]);

    const summarySubtitle = hasBaseline && changes?.baseline?.id
        ? `vs baseline #${changes.baseline.id}`
        : "no baseline available";

    function handleHostToggle(ip) { setExpandedIp(prev => prev === ip ? null : ip); }

    function handleAcknowledge(key, acknowledged) {
        const activeScanId = drilldownFromScan ? selectedScanId : latestCompletedScanId;
        if (!activeScanId || !updateDiffAcknowledgement) return;
        updateDiffAcknowledgement(activeScanId, key, acknowledged);
    }

    return (
        <PageShell banner={banners.changes ? <InlineBanner banner={banners.changes} /> : null}>
            <div className="changes-screen-layout">
                <div className="changes-screen-hero">
                    <PageSection title="diff overview" sub="baseline comparison for the selected completed scan" flush>
                        <ListRegion className="changes-overview-region">
                            <StateNotice kind={diffState.kind} message={diffState.message} compact />
                        </ListRegion>
                    </PageSection>
                    <TopologySection
                        topologyState={effectiveTopologyState}
                        topologyHostStateMap={topologyHostStateMap}
                        presenceStatusByIp={presenceStatusByIp}
                        mergedView={!drilldownFromScan}
                    />
                </div>
                <ChangeSummarySection
                    activeDiff={activeDiff}
                    changes={changes}
                    summaryChips={summaryChips}
                    summary={summary}
                    summarySubtitle={summarySubtitle}
                    drilldownFromScan={drilldownFromScan}
                    selectedScanId={selectedScanId}
                />
                {activeDiff?.kind === "ready" && hasBaseline && hasChanges ? (
                    <PageSection title="changed hosts" sub="host and port deltas only, not topology structure" flush>
                        <ListRegion className="changes-list-region">
                            <div className="changes-host-list">
                                {diffHosts.map(host => (
                                    <HostEntry
                                        key={host.ip}
                                        host={host}
                                        isOpen={expandedIp === host.ip}
                                        onToggle={() => handleHostToggle(host.ip)}
                                        onAcknowledge={handleAcknowledge}
                                        hosts={hosts}
                                    />
                                ))}
                            </div>
                        </ListRegion>
                    </PageSection>
                ) : null}
            </div>
        </PageShell>
    );
}
