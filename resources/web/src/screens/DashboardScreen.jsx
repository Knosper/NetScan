import React from "react";
import { StateBadge, Panel, Button, Chip, ListRow } from "../atoms.jsx";
import { PageShell } from "../components/PageShell.jsx";
import { formatDateTime, getHostLabel, getHostMeta } from "../ui/helpers.js";
import { StateNotice, InlineBanner } from "../ui/notices.jsx";
import { HostMetaBadges } from "../components/HostList.jsx";
import { ProfileSearchPicker } from "../components/ProfileSearchPicker.jsx";
import { ProgressBar } from "../components/ProgressBar.jsx";
import { fetchWithTimeout } from "../api.js";
import { useScanContext } from "../ScanContext.jsx";
import { useHostContext } from "../HostContext.jsx";
import {
  FINISHING_SCAN_TAIL_MS,
  LIVE_FEED_DETAIL_TIMEOUT_MS,
  LIVE_FEED_REFRESH_MS
} from "../constants/timing.js";

function makeActivationKeyDown(callback) {
  return function(event) {
    if (event.key !== "Enter" && event.key !== " ") return;
    event.preventDefault();
    callback();
  };
}

function clampProgress(progress) {
  if (!Number.isFinite(progress))
    return null;

  return Math.max(0, Math.min(100, Math.round(progress)));
}

function dashboardRowClassName(variant, clickable) {
  return [
    "dashboard-list-row",
    `dashboard-list-row-${variant}`,
    clickable ? "dashboard-list-row-clickable" : ""
  ].filter(Boolean).join(" ");
}

function normalizeHostFeedEntries(payload) {
  const rawHosts = Array.isArray(payload?.hosts) ? payload.hosts : [];

  const seen = new Set();
  const entries = [];
  rawHosts.forEach(host => {
    const ip = host?.ip || "";
    const name = host?.hostname || host?.name || "";
    const openPortCount = typeof host?.openPortCount === "number" ? host.openPortCount : 0;
    const key = `${ip}|${name}|${openPortCount}`;
    if (seen.has(key))
      return;

    seen.add(key);
    entries.push({
      ip: ip || "unknown",
      name,
      status: "up",
      openPortCount
    });
  });

  return entries;
}

function buildRunningScanKey(runningScans) {
  const scanIds = (Array.isArray(runningScans) ? runningScans : [])
    .map(scan => Number(scan?.id || 0))
    .filter(id => id > 0);
  return Array.from(new Set(scanIds)).join(",");
}

function scanIdsFromKey(runningScanKey) {
  return runningScanKey
    ? runningScanKey.split(",").map(id => Number(id)).filter(id => id > 0)
    : [];
}

async function fetchLiveScanFeed(scanId, signal) {
  const response = await fetchWithTimeout(
    `/api/scans/${scanId}`,
    {signal},
    LIVE_FEED_DETAIL_TIMEOUT_MS
  );
  if (!response.ok)
    return [scanId, { hosts: [], loading: false, error: `scan detail failed (${response.status})` }];

  const payload = await response.json();
  return [scanId, {
    hosts: normalizeHostFeedEntries(payload),
    loading: false,
    error: null
  }];
}

function buildLiveFeedState({ previous, uniqueScanIds, updates }) {
  const next = {};
  updates.forEach(([scanId, data]) => {
    next[scanId] = {
      hosts: data.hosts,
      loading: data.loading,
      error: data.error
    };
  });

  uniqueScanIds.forEach(scanId => {
    if (!next[scanId] && previous[scanId])
      next[scanId] = previous[scanId];
  });

  return next;
}

function buildInitialLiveFeedState(previous, uniqueScanIds) {
  const next = {};
  uniqueScanIds.forEach(scanId => {
    next[scanId] = previous[scanId] || { hosts: [], loading: true };
  });
  return next;
}

function buildLiveFeedUpdates(results, uniqueScanIds) {
  return results.map((result, index) => (
    result.status === "fulfilled"
      ? result.value
      : [uniqueScanIds[index], { hosts: [], loading: false, error: "feed unavailable" }]
  ));
}

function createLiveScanFeedRefresher({ uniqueScanIds, controller, cancelledRef, setFeedByScanId }) {
  let fetching = false;
  return async function refresh() {
    if (fetching)
      return;
    fetching = true;
    try {
      const results = await Promise.allSettled(
        uniqueScanIds.map(scanId => fetchLiveScanFeed(scanId, controller.signal))
      );
      if (cancelledRef.current)
        return;
      const updates = buildLiveFeedUpdates(results, uniqueScanIds);
      setFeedByScanId(previous => buildLiveFeedState({ previous, uniqueScanIds, updates }));
    } finally {
      fetching = false;
    }
  };
}

function beginLiveScanFeed({ uniqueScanIds, setFeedByScanId }) {
  const cancelledRef = { current: false };
  const controller = new AbortController();
  setFeedByScanId(previous => buildInitialLiveFeedState(previous, uniqueScanIds));

  const refresh = createLiveScanFeedRefresher({
    uniqueScanIds,
    controller,
    cancelledRef,
    setFeedByScanId
  });

  refresh();
  const intervalId = window.setInterval(refresh, LIVE_FEED_REFRESH_MS);
  return () => {
    cancelledRef.current = true;
    controller.abort();
    window.clearInterval(intervalId);
  };
}

function useLiveScanFeed(runningScans) {
  const [feedByScanId, setFeedByScanId] = React.useState({});
  const runningScanKey = React.useMemo(() => buildRunningScanKey(runningScans), [runningScans]);

  React.useEffect(() => {
    const uniqueScanIds = scanIdsFromKey(runningScanKey);

    if (!uniqueScanIds.length) {
      setFeedByScanId({});
      return undefined;
    }

    return beginLiveScanFeed({
      uniqueScanIds,
      setFeedByScanId
    });
  }, [runningScanKey]);

  return feedByScanId;
}

function ActiveScanEntry({ scan, scanFeed, abortingScans, handleAbortScan }) {
  const scanState = String(scan?.state || "queued").toLowerCase();
  const scanActive = scanState === "queued" || scanState === "running";
  const scanCompleted = scanState === "completed";
  const scanTerminatedAbnormally = scanState === "aborted" || scanState === "failed"
    || scanState === "canceled" || scanState === "cancelled";
  const chunkStatusText = scanActive && typeof scan?.command === "string"
    && scan.command.startsWith("chunked nmap scan")
    ? scan.command
    : "";
  const scanStatusText = chunkStatusText || scan.message || "";
  const feedHosts = Array.isArray(scanFeed?.hosts) ? scanFeed.hosts : [];
  const feedLoading = Boolean(scanFeed?.loading);

  return (
    <div className="running-scan-panel running-scan-panel-inline">
      <div className="running-scan-head">
        <span className="running-scan-id">scan #{scan.id}</span>
        <StateBadge state={scanState} />
        {scan.target ? <Chip tone="info">{scan.target}</Chip> : null}
        {scanStatusText && <span className="running-scan-cmd">{scanStatusText}</span>}
        {scanActive ? (
          <Button type="button" variant="danger" size="sm"
            onClick={() => handleAbortScan(scan.id)} disabled={abortingScans[scan.id]}>
            {abortingScans[scan.id] ? "aborting" : "abort"}
          </Button>
        ) : null}
      </div>
      {scanTerminatedAbnormally ? null : (
        <ProgressBar progress={scan?.progress} isDeterminate={scanActive || scanCompleted}
          isActive={scanActive} resetKey={scan?.id} />
      )}
      {scanActive ? (
        <div className="running-scan-feed">
          <div className="running-scan-feed-head">
            <span>live host feed</span>
            {feedLoading ? <span className="running-scan-feed-meta">updating…</span> : null}
          </div>
          {feedHosts.length ? (
            <div className="running-scan-feed-list">
              {feedHosts.map((host, index) => (
                <div className="running-scan-feed-row"
                  key={`${scan.id}-${host.ip}-${host.name}-${host.openPortCount}-${index}`}>
                  <span className="running-scan-feed-ip">{host.ip}</span>
                  <span className="running-scan-feed-status">{host.status}</span>
                  <span className="running-scan-feed-ports">
                    {host.openPortCount > 0 ? `${host.openPortCount} open port${host.openPortCount !== 1 ? "s" : ""}` : "no open ports"}
                  </span>
                </div>
              ))}
            </div>
          ) : (
            <div className="running-scan-feed-empty">
              {feedLoading ? "waiting for discovered hosts" : "no hosts discovered yet"}
            </div>
          )}
        </div>
      ) : null}
    </div>
  );
}

function ScanFormPanel({ form, setForm, loading, profileOptions, applyScanProfile, handleStartScan,
  displayedRunningScans, liveScanFeedById, abortingScans, handleAbortScan, backendUnavailable,
  offlineMessage, topBanner }) {
  function applyProfileSelection(profile) {
    if (profile) applyScanProfile(profile);
  }

  return (
    <Panel title="scan operations" accent>
      <div className="scan-operations-layout">
        <div className="scan-operations-queue">
          {!backendUnavailable && topBanner ? <InlineBanner banner={topBanner} /> : null}
          <form className="scan-form quick-scan-form" onSubmit={handleStartScan}>
            <input type="text" className="field-input" value={form.target}
              placeholder="192.168.1.1 · 192.168.1.0/24 · 10.0.0.1-10"
              onChange={e => setForm(p => ({ ...p, target: e.target.value }))} />
            <input type="text" className="field-input" value={form.ports} disabled={form.hostDiscoveryOnly}
              placeholder="22,80,443"
              onChange={e => setForm(p => ({ ...p, ports: e.target.value }))} />
            <div className="quick-scan-bottom-row">
              <div className="profile-selector" role="group" aria-label="scan profiles">
                <ProfileSearchPicker options={profileOptions} onSelect={applyProfileSelection}
                  triggerLabel="select profile" />
              </div>
              <div className="quick-scan-option-chips">
                <button type="button"
                  className={"button button-ghost" + (form.hostDiscoveryOnly ? " is-active" : "")}
                  onClick={() => setForm(p => ({
                    ...p,
                    hostDiscoveryOnly: !p.hostDiscoveryOnly,
                    ports: !p.hostDiscoveryOnly ? "" : p.ports
                  }))}>
                  HostScan
                </button>
              </div>
            </div>
            <button type="submit" className="button button-primary" disabled={loading.starting}>
              {loading.starting ? "starting" : "start scan"}
            </button>
          </form>
        </div>
        <div className="scan-operations-running">
          {displayedRunningScans.length ? (
            <div className="active-scan-list">
              {displayedRunningScans.map(entry => (
                <ActiveScanEntry key={entry.id || `${entry.target}-${entry.createdAt}`}
                  scan={entry} scanFeed={liveScanFeedById[entry.id] || null}
                  abortingScans={abortingScans} handleAbortScan={handleAbortScan} />
              ))}
            </div>
          ) : (
            <StateNotice kind={backendUnavailable ? "offline" : "empty"}
              message={backendUnavailable ? offlineMessage : "no active scan"} compact />
          )}
        </div>
      </div>
    </Panel>
  );
}

function RecentScansPanel({ go, visibleScans, recentScans, showAllScans, setShowAllScans }) {
  function openScanDetails(scanId) {
    if (!scanId) return;
    go("changes", { source: "scans", scanId });
  }

  return (
    <Panel title="recent scans" flush>
      <div className="dashboard-list">
        {visibleScans.length ? visibleScans.map(entry => {
          const scanId = Number(entry?.id || 0) || null;
          const clickable = scanId !== null;
          const rowClassName = dashboardRowClassName("scans", clickable);
          return (
            <ListRow className={rowClassName} key={entry.id || `${entry.target}-${entry.createdAt}`}
              role={clickable ? "button" : undefined}
              tabIndex={clickable ? 0 : undefined}
              onClick={clickable ? () => openScanDetails(scanId) : undefined}
              onKeyDown={clickable ? makeActivationKeyDown(() => openScanDetails(scanId)) : undefined}>
              <strong className="tone-strong">#{entry.id ?? "-"}</strong>
              <span className="dashboard-list-main">{entry.target || "-"}</span>
              <span className="dashboard-scan-state">
                <StateBadge state={entry.state || "idle"} />
                {typeof entry.progress === "number"
                  ? <Chip tone="info">{clampProgress(entry.progress)}%</Chip>
                  : null}
              </span>
              <span className="dashboard-list-meta">{formatDateTime(entry.createdAt)}</span>
            </ListRow>
          );
        }) : <StateNotice kind="empty" message="no scans yet" compact />}
      </div>
      {recentScans.length > 3 && (
        <ListRow className="dashboard-list-row dashboard-list-row-scans dashboard-list-toggle">
          <button type="button" className="button button-ghost button-sm"
            onClick={() => setShowAllScans(!showAllScans)}>
            {showAllScans ? "show less" : `show all (${recentScans.length})`}
          </button>
        </ListRow>
      )}
    </Panel>
  );
}

function TopHostsPanel({ go, topHosts, hosts, portsByHost }) {
  function openHostDetails(hostIp) {
    if (!hostIp) return;
    go("hosts", { hostIp });
  }

  return (
    <Panel title="top hosts" flush>
      <div className="dashboard-list">
        {topHosts.length ? topHosts.map(host => {
          const hostMeta = getHostMeta(host.ip, hosts);
          const label = getHostLabel(host.ip, hosts);
          const hostIp = host?.ip || null;
          const clickable = Boolean(hostIp);
          const rowClassName = dashboardRowClassName("hosts", clickable);
          return (
            <ListRow className={rowClassName} key={host.ip || host.name}
              role={clickable ? "button" : undefined}
              tabIndex={clickable ? 0 : undefined}
              onClick={clickable ? () => openHostDetails(hostIp) : undefined}
              onKeyDown={clickable ? makeActivationKeyDown(() => openHostDetails(hostIp)) : undefined}>
              <strong className="tone-strong">{label}</strong>
              <HostMetaBadges meta={hostMeta} />
              <span className="dashboard-list-main">{host.name || ""}</span>
              <Chip tone="accent">{host.openPortCount ?? host.portCount ?? portsByHost[host.ip] ?? 0} open</Chip>
              <span className="dashboard-list-meta">{formatDateTime(host.last_seen || host.lastSeenAt || null)}</span>
            </ListRow>
          );
        }) : <StateNotice kind="empty" message="no hosts available" compact />}
      </div>
    </Panel>
  );
}

function useFinishingScans(runningScans, recentScans) {
  const [finishingScans, setFinishingScans] = React.useState([]);
  const previousRunningScansRef = React.useRef([]);
  const timeoutIdsRef = React.useRef([]);

  React.useEffect(() => () => {
    timeoutIdsRef.current.forEach(timeoutId => window.clearTimeout(timeoutId));
    timeoutIdsRef.current = [];
  }, []);

  React.useEffect(() => {
    const currentIds = new Set(runningScans.map(scan => scan?.id).filter(Boolean));
    const completedScans = previousRunningScansRef.current
      .filter(scan => scan?.id && !currentIds.has(scan.id))
      .map(scan => {
        const latest = recentScans.find(s => s.id === scan.id);
        const finalState = latest?.state || "completed";
        return {...scan, state: finalState, progress: finalState === "completed" ? 100 : scan.progress};
      });

    previousRunningScansRef.current = runningScans;

    if (!completedScans.length)
      return undefined;

    setFinishingScans(previous => {
      const merged = new Map(previous.map(scan => [scan.id, scan]));
      completedScans.forEach(scan => merged.set(scan.id, scan));
      return Array.from(merged.values());
    });

    const timeoutId = window.setTimeout(() => {
      timeoutIdsRef.current = timeoutIdsRef.current.filter(id => id !== timeoutId);
      setFinishingScans(previous => previous.filter(scan => !completedScans.some(e => e.id === scan.id)));
    }, FINISHING_SCAN_TAIL_MS);
    timeoutIdsRef.current.push(timeoutId);

    return () => {
      window.clearTimeout(timeoutId);
      timeoutIdsRef.current = timeoutIdsRef.current.filter(id => id !== timeoutId);
    };
  }, [runningScans, recentScans]);

  return finishingScans;
}

export function DashboardScreen({
  go,
  backendUnavailable,
  offlineMessage,
  topBanner,
  loading,
  dashboard
}) {
  const {
    scans, activeScans, form, setForm, scanProfileOptions, applyScanProfile,
    handleStartScan, abortingScans, handleAbortScan
  } = useScanContext();
  const { hosts } = useHostContext();
  const [showAllScans, setShowAllScans] = React.useState(false);
  const changes = dashboard.changes || dashboard.changeSummary || {};
  const portsByHost = Array.isArray(dashboard.ports)
    ? dashboard.ports.reduce((acc, entry) => {
        const hostIp = entry?.host;
        if (!hostIp) return acc;
        acc[hostIp] = (acc[hostIp] || 0) + 1;
        return acc;
      }, {})
    : {};
  const recentScans = Array.isArray(scans) && scans.length
    ? scans
    : (Array.isArray(dashboard.scans) ? dashboard.scans : []);
  const runningScans = Array.isArray(activeScans)
    ? activeScans
    : recentScans.filter(entry => {
        const state = String(entry?.state || "").toLowerCase();
        return state === "running" || state === "queued";
      });

  const finishingScans = useFinishingScans(runningScans, recentScans);
  const displayedRunningScans = [
    ...runningScans,
    ...finishingScans.filter(scan => !runningScans.some(entry => entry?.id === scan.id))
  ];
  const liveScanFeedById = useLiveScanFeed(runningScans);
  const visibleScans = showAllScans ? recentScans : recentScans.slice(0, 3);
  const topHosts = Array.isArray(dashboard.hosts)
    ? dashboard.hosts.filter(host => (portsByHost[host.ip] ?? 0) > 0).slice(0, 5)
    : [];
  const profileOptions = Array.isArray(scanProfileOptions) ? scanProfileOptions : [];

  return (
    <PageShell>
      <div className="dashboard-workspace">
        <div className="dashboard-main-grid">
          <div className="dashboard-panel-primary">
            <ScanFormPanel
              form={form} setForm={setForm} loading={loading}
              profileOptions={profileOptions} applyScanProfile={applyScanProfile}
              handleStartScan={handleStartScan} displayedRunningScans={displayedRunningScans}
              liveScanFeedById={liveScanFeedById}
              abortingScans={abortingScans} handleAbortScan={handleAbortScan}
              backendUnavailable={backendUnavailable} offlineMessage={offlineMessage}
              topBanner={topBanner}
            />
          </div>
          <div className="dashboard-panel-recent">
            <RecentScansPanel
              go={go}
              visibleScans={visibleScans} recentScans={recentScans}
              showAllScans={showAllScans} setShowAllScans={setShowAllScans}
            />
          </div>
          <TopHostsPanel go={go} topHosts={topHosts} hosts={hosts} portsByHost={portsByHost} />
        </div>
      </div>
    </PageShell>
  );
}
