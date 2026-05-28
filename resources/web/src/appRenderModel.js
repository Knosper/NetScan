import { DashboardScreen } from "./screens/DashboardScreen.jsx";
import { HostsScreen } from "./screens/HostsScreen.jsx";
import { SettingsScreen } from "./screens/SettingsScreen.jsx";
import { ChangesScreen } from "./screens/ChangesScreen.jsx";
import { ServicesScreen } from "./screens/ServicesScreen.jsx";
import { PresenceScreen } from "./screens/PresenceScreen.jsx";
import {
    toneForHealthStatus,
    toneForScanState
} from "./ui/helpers.js";
import { buildScanStatusValue } from "./hooks/useScanPolling.jsx";
import { NAV } from "./ui/navigation.js";

const SCREEN_COMPONENTS = {
    dashboard: DashboardScreen,
    hosts: HostsScreen,
    presence: PresenceScreen,
    services: ServicesScreen,
    changes: ChangesScreen,
    settings: SettingsScreen
};

function isActiveScanState(state)
{
    const normalized = String(state || "").toLowerCase();
    return normalized === "queued" || normalized === "running";
}

function dedupeScans(scans)
{
    const merged = new Map();

    (Array.isArray(scans.dashboardScanRows) ? scans.dashboardScanRows : []).forEach(scan => {
        if (!scan?.id)
            return;
        merged.set(scan.id, scan);
    });

    (Array.isArray(scans.scanRows) ? scans.scanRows : []).forEach(scan => {
        if (!scan?.id)
            return;
        const previous = merged.get(scan.id) || {};
        merged.set(scan.id, {...previous, ...scan});
    });

    if (scans.activeScan?.id) {
        const previous = merged.get(scans.activeScan.id) || {};
        merged.set(scans.activeScan.id, {...previous, ...scans.activeScan});
    }

    return Array.from(merged.values());
}

function sortScansById(scans)
{
    return scans.slice().sort((left, right) => (right?.id || 0) - (left?.id || 0));
}

function mergeScanCollections(scanRows, dashboardScanRows, activeScan)
{
    return sortScansById(dedupeScans({scanRows, dashboardScanRows, activeScan}));
}

function buildActiveScanList(scans)
{
    return (Array.isArray(scans) ? scans : []).filter(scan => isActiveScanState(scan?.state || "idle"));
}

function buildProfileOption(profile, source)
{
    return {
        id: `${source}-${profile.id}`,
        source,
        name: profile.name || "unnamed profile",
        target: profile.target || "",
        ports: profile.ports || "",
        hostDiscoveryOnly: Boolean(profile.hostDiscoveryOnly)
    };
}

function resolveHostsForDisplay({dashboard, hosts, hostsLoading, hostSearch})
{
    if (hostsLoading || hostSearch.trim() || hosts.length > 0 || !Array.isArray(dashboard?.hosts))
        return hosts;

    return dashboard.hosts.map(host => ({
        id: host.id,
        ip: host.ip,
        name: host.name,
        scanCount: 0,
        lastSeenAt: "",
        lastScanId: null,
        openPortCount: 0
    }));
}

function calcChangesCount(changes)
{
    const addedHosts = Number(changes.addedHosts ?? (changes.newHosts || []).length ?? 0);
    const addedPorts = Number(changes.addedPorts ?? (changes.newOpenPorts || []).length ?? 0);
    return changes.hasBaseline ? addedHosts + addedPorts : 0;
}

function buildNavCounts({dashboard, hostsForDisplay, presenceTrackers, totalOpenPorts, changes})
{
    return {
        dashboard: dashboard?.stats?.services ?? 0,
        hosts: hostsForDisplay.length,
        presence: presenceTrackers.length,
        services: totalOpenPorts,
        changes: calcChangesCount(changes)
    };
}

function buildAppShellProps(model, appMeta)
{
    const {
        screen, go, current, isDashboardScreen, navCounts, commandInputRef, setForm,
        handleStartScan, backendTone, nmapTone, dbTone, backendUnavailable, checks,
        currentScanActive, scanStatusValue, sessionUptime, scanTone, scanStatus,
        scans, hosts, totalPorts, utcTime, pollInterval, theme
    } = model;

    return {
        screen, go, navCounts, commandInputRef, setForm,
        handleStartScan, backendUnavailable, checks,
        sessionUptime, appVersion: appMeta.appVersion,
        schemaVersion: appMeta.schemaVersion, scanTone, scanStatus,
        utcTime, pollInterval, theme,
        navigation: { isDashboardScreen, go, current },
        counts: { hostsCount: hosts.length, totalPorts, navCounts, scansCount: scans.length },
        status: { backendTone, nmapTone, dbTone, currentScanActive, scanStatusValue },
        scan: { scanTone, backendUnavailable, scanStatus, currentScanActive, scanStatusValue },
        system: { pollInterval, utcTime }
    };
}

function buildScreenProps(model)
{
    const {
        go, screenExtra, dashboard, backendUnavailable, topBanner, offlineMessage,
        screenLoading, profiles, profileForm, setProfileForm, handleCreateProfile,
        handleCancelProfileEdit, runProfile, startProfileEdit, deleteProfile,
        settings, setSettings, settingsRowErrors, setSettingsRowErrors,
        handleSaveSettings, handleShutdown, handlePruneClosedPorts,
        banners, health
    } = model;

    return {
        go, screenExtra, dashboard, backendUnavailable, topBanner, offlineMessage,
        loading: screenLoading, profiles, profileForm, setProfileForm, handleCreateProfile,
        handleCancelProfileEdit, runProfile, startProfileEdit, deleteProfile,
        settings, setSettings, settingsRowErrors, setSettingsRowErrors,
        handleSaveSettings, handleShutdown, handlePruneClosedPorts,
        banners, localIp: health?.local_ip || ""
    };
}

function buildScanContextValue(model)
{
    const {
        mergedScans, scanStatus, scanDiffs, scanTopologies, abortingScans, handleAbortScan,
        handleStartScan, loadScanDiff, loadScanTopology, loadScanTopologies, updateDiffAcknowledgement,
        form, setForm, applyScanProfile, scanProfileOptions, activeScans
    } = model;

    return {
        scans: mergedScans, scanStatus, scanDiffs, scanTopologies, abortingScans, handleAbortScan,
        handleStartScan, loadScanDiff, loadScanTopology, loadScanTopologies, updateDiffAcknowledgement,
        form, setForm, applyScanProfile, scanProfileOptions, activeScans
    };
}

function buildHostContextValue(model)
{
    const {
        hostsForDisplay, expandedHosts, hostDetails, hostDetailLoading, hostScanDetails,
        hostScanDetailLoading, rescanningHosts, hostSearch, setHostSearch, showOnlyOpenPorts,
        setShowOnlyOpenPorts, hideRedundantHistory, setHideRedundantHistory, hostPage,
        hostTotalPages, onGoToPage, toggleHostExpanded, handleRescanHost, handlePatchHostMeta,
        handleDeleteHostMetaField
    } = model;

    return {
        hosts: hostsForDisplay, expandedHosts, hostDetails, hostDetailLoading, hostScanDetails,
        hostScanDetailLoading, rescanningHosts, hostSearch, setHostSearch, showOnlyOpenPorts,
        setShowOnlyOpenPorts, hideRedundantHistory, setHideRedundantHistory, hostPage,
        hostTotalPages, onGoToPage, toggleHostExpanded, handleRescanHost, handlePatchHostMeta,
        handleDeleteHostMetaField
    };
}

function buildPresenceContextValue(model)
{
    const {
        presenceTrackers, presenceLoading, presenceCheckingTrackerIds, presenceUpdatingTrackerIds,
        createPresenceTracker, updatePresenceTrackerEnabled, deletePresenceTracker, checkPresenceTracker
    } = model;

    return {
        presenceTrackers, presenceLoading, presenceCheckingTrackerIds, presenceUpdatingTrackerIds,
        createPresenceTracker, updatePresenceTrackerEnabled, deletePresenceTracker, checkPresenceTracker
    };
}

function buildSchedulerContextValue(model)
{
    const {
        schedulerJobs, schedulerLoading, createSchedulerJob, deleteSchedulerJob, setSchedulerJobEnabled
    } = model;

    return { schedulerJobs, schedulerLoading, createSchedulerJob, deleteSchedulerJob, setSchedulerJobEnabled };
}

function buildLogPanelProps(model)
{
    const {logExpanded, logPanelHeight, logResizing, logEntries, handleLogResizeStart, handleLogPanelToggle} = model;
    return {logExpanded, logPanelHeight, logResizing, logEntries, handleLogResizeStart, handleLogPanelToggle};
}

function deriveRenderModel(model)
{
    const currentScan = model.scanStatus.scan;
    const currentScanId = currentScan?.id || null;
    const currentScanActive = isActiveScanState(model.scanStatus.status);
    const currentScanRunning = model.scanStatus.status === "running";
    const scanProgress = typeof currentScan?.progress === "number" ? Math.round(currentScan.progress) : null;
    const changes = model.dashboard?.changes || model.dashboard?.changeSummary || {};
    const mergedScans = mergeScanCollections(model.scans, model.dashboard?.scans || [], currentScan);
    const activeScans = buildActiveScanList(mergedScans);
    const hostsForDisplay = resolveHostsForDisplay(model);
    const totalOpenPorts = hostsForDisplay.reduce((sum, host) => sum + Number(host.openPortCount ?? 0), 0);
    const navCounts = {
        ...buildNavCounts({...model, hostsForDisplay, totalOpenPorts, changes}),
        settings: model.settings?.log_level || "cfg"
    };
    const checks = model.health?.checks || {};
    const topBanner = model.banners.top;

    return {
        ...model, currentScanActive, currentScanRunning, scanProgress, changes, mergedScans,
        activeScans, hostsForDisplay, totalOpenPorts, navCounts, checks, topBanner,
        current: NAV.find(item => item.id === model.screen),
        Screen: SCREEN_COMPONENTS[model.screen] || DashboardScreen,
        isDashboardScreen: model.screen === "dashboard",
        scanProfileOptions: model.profiles.map(profile => buildProfileOption(profile, "saved")),
        offlineMessage: model.health.message || topBanner?.text || "backend not reachable",
        backendTone: model.backendUnavailable ? "danger" : "success",
        nmapTone: model.backendUnavailable ? "muted" : toneForHealthStatus(checks.nmap, false),
        dbTone: model.backendUnavailable ? "muted" : toneForHealthStatus(checks.database, false),
        scanTone: toneForScanState(model.scanStatus.status, model.backendUnavailable),
        pollInterval: currentScanActive ? "1s" : "5s",
        totalPorts: model.dashboard?.stats?.ports ?? 0,
        screenLoading: {...model.loading, hosts: model.hostsLoading},
        scanStatusValue: buildScanStatusValue({
            backendUnavailable: model.backendUnavailable, scanStatus: model.scanStatus,
            currentScanId, currentScanRunning, scanProgress
        })
    };
}

export function buildAppRenderModel(model, appMeta)
{
    const enriched = deriveRenderModel(model);

    return {
        Screen: enriched.Screen,
        shellProps: buildAppShellProps(enriched, appMeta),
        screenProps: buildScreenProps(enriched),
        logPanelProps: buildLogPanelProps(enriched),
        scanContextValue: buildScanContextValue(enriched),
        hostContextValue: buildHostContextValue(enriched),
        presenceContextValue: buildPresenceContextValue(enriched),
        schedulerContextValue: buildSchedulerContextValue(enriched)
    };
}
