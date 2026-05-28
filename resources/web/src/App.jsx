import React, {useCallback, useEffect, useRef, useState} from "react";
import { setApiKeyPromptListener } from "./apiKey.js";
import { ApiKeyModal } from "./components/ApiKeyModal.jsx";
import { useScreenRouter } from "./hooks/useScreenRouter.jsx";
import { AppContext, useAppContext } from "./AppContext.jsx";
import { ScanContext } from "./ScanContext.jsx";
import { HostContext } from "./HostContext.jsx";
import { PresenceContext } from "./PresenceContext.jsx";
import { SchedulerContext } from "./SchedulerContext.jsx";
import { fetchWithTimeout, readOptionalJson } from "./api.js";
import { buildBanner } from "./ui/helpers.js";
import { SetupScreen } from "./screens/SetupScreen.jsx";
import { useDashboard } from "./hooks/useDashboard.jsx";
import { useScans } from "./hooks/useScans.jsx";
import { useHosts } from "./hooks/useHosts.jsx";
import { useProfiles } from "./hooks/useProfiles.jsx";
import { usePresenceTrackers } from "./hooks/usePresenceTrackers.jsx";
import { useSchedulerJobs } from "./hooks/useSchedulerJobs.jsx";
import { useAppStatus } from "./hooks/useAppStatus.jsx";
import { useKeyboardShortcuts } from "./hooks/useKeyboardShortcuts.jsx";
import { usePolling } from "./hooks/usePolling.jsx";
import { useScanPolling } from "./hooks/useScanPolling.jsx";
import { useLogPanel } from "./hooks/useLogPanel.jsx";
import { useLogEntries } from "./hooks/useLogEntries.jsx";
import { useScanHandlers } from "./hooks/useScanHandlers.jsx";
import { useSettingsHandlers, EMPTY_PROFILE_FORM } from "./hooks/useSettingsHandlers.jsx";
import { createSettingsModel } from "./settingsTargets.js";
import { useHealthAndRefresh } from "./hooks/useHealthAndRefresh.jsx";
import { useProfileHandlers } from "./hooks/useProfileHandlers.jsx";
import { useUtcClock, useSessionUptime } from "./hooks/useClock.jsx";
import { AppShell } from "./components/AppShell.jsx";
import { LogPanel } from "./components/LogPanel.jsx";
import { buildAppRenderModel } from "./appRenderModel.js";
import {
    BANNER_AUTO_HIDE_MS,
    HOST_FILTER_DEBOUNCE_MS,
    PRESENCE_TRACKERS_REFRESH_MS
} from "./constants/timing.js";

const APP_VERSION = "0.4.2";
const SCHEMA_VERSION = "v1";
const THEME_STORAGE_KEY = "netscan:theme";
const DEFAULT_THEME = "dark";
const AVAILABLE_THEMES = [
    { id: "dark", label: "dark", description: "original dark theme" },
    { id: "light", label: "light", description: "light theme" },
    { id: "vivid-dark", label: "vivid dark", description: "brighter, more colorful dark theme" }
];
const VALID_SCREENS = new Set(["dashboard", "hosts", "presence", "services", "changes", "settings"]);

function normalizeTheme(theme)
{
    const value = String(theme || "").toLowerCase();
    return AVAILABLE_THEMES.some(option => option.id === value) ? value : DEFAULT_THEME;
}

function getInitialTheme()
{
    return normalizeTheme(localStorage.getItem(THEME_STORAGE_KEY) || DEFAULT_THEME);
}

function useAppOrchestration()
{
    const { dashboard, loadDashboard } = useDashboard();
    const [loading, setLoading] = useState({
        scans: true,
        hosts: true,
        settings: true,
        starting: false,
        savingSettings: false
    });
    const {
        scans,
        scanDiffs,
        scanTopologies,
        abortingScans,
        loadScans,
        loadScanDiff,
        updateDiffAcknowledgement,
        loadScanTopology,
        loadScanTopologies,
        handleAbortScan
    } = useScans(setLoading);

    const {
        profiles,
        loadProfiles,
        createProfile,
        updateProfile,
        deleteProfile,
        runProfile
    } = useProfiles();

    const {
        trackers: presenceTrackers,
        loading: presenceLoading,
        checkingTrackerIds: presenceCheckingTrackerIds,
        updatingTrackerIds: presenceUpdatingTrackerIds,
        loadPresenceTrackers,
        createTracker: createPresenceTracker,
        updateTrackerEnabled: updatePresenceTrackerEnabled,
        deleteTracker: deletePresenceTracker,
        checkTracker: checkPresenceTracker
    } = usePresenceTrackers();

    const {
        jobs: schedulerJobs,
        loading: schedulerLoading,
        loadJobs: loadSchedulerJobs,
        createJob: createSchedulerJob,
        deleteJob: deleteSchedulerJob,
        setJobEnabled: setSchedulerJobEnabled
    } = useSchedulerJobs();

    const {
        hosts,
        loading: hostsLoading,
        expandedHosts,
        setExpandedHosts,
        hostDetails,
        hostDetailLoading,
        hostScanDetails,
        hostScanDetailLoading,
        rescanningHosts,
        setRescanningHosts,
        hostSearch,
        setHostSearch,
        hideRedundantHistory,
        setHideRedundantHistory,
        hostPage,
        hostTotalPages,
        loadHosts,
        goToPage,
        loadHostDetail,
        loadHostScanDetail,
        toggleHostExpanded,
        patchHostMeta,
        deleteHostMetaField
    } = useHosts();

    const {
        banners,
        backendUnavailable,
        setBanner,
        markBackendUnavailable,
        clearBackendUnavailable,
        cleanupBannerTimers,
        theme
    } = useAppContext();

    const [settings, setSettings] = useState(() => createSettingsModel());
    const [settingsRowErrors, setSettingsRowErrors] = useState({});
    const [form, setForm] = useState({
        target: "",
        ports: "",
        hostDiscoveryOnly: false
    });
    const [showOnlyOpenPorts, setShowOnlyOpenPorts] = useState(false);

    const [profileForm, setProfileForm] = useState(EMPTY_PROFILE_FORM);

    const { screen, screenExtra, go } = useScreenRouter(VALID_SCREENS);

    const utcTime = useUtcClock();
    const sessionUptime = useSessionUptime();

    const { scanStatus, setScanStatus, pollScanStatus } = useScanPolling({
        setBanner,
        clearBackendUnavailable,
        markBackendUnavailable,
        loadScans,
        loadHosts,
        loadDashboard,
        loadScanTopology
    });

    const { logExpanded, logPanelHeight, logResizing, handleLogResizeStart, handleLogPanelToggle } = useLogPanel();
    const { logEntries } = useLogEntries(backendUnavailable, scanStatus);

    const commandInputRef = useRef(null);

    const { handleStartScan, handleRescanHost } = useScanHandlers({
        form,
        setScanStatus,
        setRescanningHosts,
        setLoading,
        loadDashboard,
        loadScans,
        loadHosts
    });

    const loadSettings = useCallback(async () => {
        setLoading(previous => ({...previous, settings: true}));

        try
        {
            const response = await fetchWithTimeout("/api/settings");
            const payload = await readOptionalJson(response);

            if (!response.ok)
            {
                setBanner("settings", buildBanner("danger",
                    payload?.message || `settings failed ${response.status}`), BANNER_AUTO_HIDE_MS);
                return;
            }

            clearBackendUnavailable();
            setSettings(createSettingsModel(payload?.settings));
            setSettingsRowErrors({});
            setBanner("settings", null);
        }
        catch (_)
        {
            markBackendUnavailable();
        }
        finally
        {
            setLoading(previous => ({...previous, settings: false}));
        }
    }, [setBanner, markBackendUnavailable, clearBackendUnavailable]);

    const { handleSaveSettings, handlePruneClosedPorts, handleShutdown } = useSettingsHandlers({
        settings,
        setSettingsRowErrors,
        setLoading,
        loadHosts,
        loadSettings
    });

    const { handleCreateProfile, handleCancelProfileEdit, startProfileEdit } = useProfileHandlers({
        profileForm,
        setProfileForm,
        createProfile,
        updateProfile
    });

    useEffect(() => {
        localStorage.setItem(THEME_STORAGE_KEY, theme);
    }, [theme]);

    const { health, refreshAll } = useHealthAndRefresh({
        loadDashboard,
        loadScans,
        loadHosts,
        loadSettings,
        loadProfiles,
        loadPresenceTrackers,
        loadSchedulerJobs,
        setBanner,
        markBackendUnavailable,
        clearBackendUnavailable
    });

    usePolling(pollScanStatus, refreshAll, cleanupBannerTimers);

    useEffect(() => {
        expandedHosts.forEach(ip => {
            if (!hostDetails[ip] && !hostDetailLoading[ip])
                loadHostDetail(ip);

            const host = hosts.find(entry => entry.ip === ip);
            if (host?.lastScanId)
                loadHostScanDetail(host.lastScanId);
        });
    }, [expandedHosts, hosts, hostDetails, hostDetailLoading, loadHostDetail, loadHostScanDetail]);

    useEffect(() => {
        const controller = typeof AbortController !== "undefined" ? new AbortController() : null;
        const timeoutId = window.setTimeout(() => {
            loadHosts({
                openPortsOnly: showOnlyOpenPorts,
                signal: controller?.signal
            });
        }, HOST_FILTER_DEBOUNCE_MS);

        return () => {
            window.clearTimeout(timeoutId);
            controller?.abort();
        };
    }, [hostSearch, showOnlyOpenPorts, loadHosts]);

    useEffect(() => {
        if (screen !== "presence" && screen !== "changes")
            return undefined;

        loadPresenceTrackers({quiet: true});
        const id = window.setInterval(() => loadPresenceTrackers({quiet: true}), PRESENCE_TRACKERS_REFRESH_MS);
        return () => window.clearInterval(id);
    }, [screen, loadPresenceTrackers]);

    useEffect(() => {
        if (screen !== "hosts")
            return;

        const hostIp = String(screenExtra?.hostIp || "").trim();
        if (!hostIp)
            return;

        setHostSearch(hostIp);
        setExpandedHosts(previous => previous.includes(hostIp) ? previous : [...previous, hostIp]);
    }, [screen, screenExtra, setExpandedHosts, setHostSearch]);

    useKeyboardShortcuts(go, commandInputRef);

    async function handlePatchHostMeta(ip, fields)
    {
        return patchHostMeta(ip, fields);
    }

    async function handleDeleteHostMetaField(ip, field)
    {
        return deleteHostMetaField(ip, field);
    }

    function applyScanProfile(profile)
    {
        setForm(previous => {
            const keepHostDiscovery = previous.hostDiscoveryOnly && !profile.hostDiscoveryOnly;
            const nextHostDiscoveryOnly = keepHostDiscovery ? true : Boolean(profile.hostDiscoveryOnly);

            return {
                target: profile.target,
                ports: nextHostDiscoveryOnly ? "" : (profile.ports || ""),
                hostDiscoveryOnly: nextHostDiscoveryOnly
            };
        });
    }

    function onGoToPage(page)
    {
        return goToPage(page, { openPortsOnly: showOnlyOpenPorts });
    }

    return buildAppRenderModel({
        screen,
        screenExtra,
        go,
        dashboard,
        scans,
        scanDiffs,
        scanTopologies,
        loadScanDiff,
        updateDiffAcknowledgement,
        loadScanTopology,
        abortingScans,
        handleAbortScan,
        hosts,
        hostsLoading,
        expandedHosts,
        hostDetails,
        hostDetailLoading,
        hostScanDetails,
        hostScanDetailLoading,
        rescanningHosts,
        hostSearch,
        setHostSearch,
        showOnlyOpenPorts,
        setShowOnlyOpenPorts,
        hideRedundantHistory,
        setHideRedundantHistory,
        hostPage,
        hostTotalPages,
        onGoToPage,
        toggleHostExpanded,
        handleRescanHost,
        handlePatchHostMeta,
        handleDeleteHostMetaField,
        profiles,
        profileForm,
        setProfileForm,
        handleCreateProfile,
        handleCancelProfileEdit,
        runProfile,
        startProfileEdit,
        deleteProfile,
        presenceTrackers,
        presenceLoading,
        presenceCheckingTrackerIds,
        presenceUpdatingTrackerIds,
        createPresenceTracker,
        updatePresenceTrackerEnabled,
        deletePresenceTracker,
        checkPresenceTracker,
        schedulerJobs,
        schedulerLoading,
        createSchedulerJob,
        deleteSchedulerJob,
        setSchedulerJobEnabled,
        banners,
        backendUnavailable,
        health,
        settings,
        setSettings,
        settingsRowErrors,
        setSettingsRowErrors,
        form,
        setForm,
        loading,
        applyScanProfile,
        handleStartScan,
        handleSaveSettings,
        handleShutdown,
        handlePruneClosedPorts,
        scanStatus,
        commandInputRef,
        sessionUptime,
        utcTime,
        logExpanded,
        logPanelHeight,
        logResizing,
        logEntries,
        handleLogResizeStart,
        handleLogPanelToggle,
        theme
    }, {
        appVersion: APP_VERSION,
        schemaVersion: SCHEMA_VERSION
    });
}

function ComposeProviders({ providers, children })
{
    return providers.reduceRight((acc, [Context, value]) => (
        <Context.Provider value={value}>{acc}</Context.Provider>
    ), children);
}

function AppInner()
{
    const {Screen, shellProps, screenProps, logPanelProps,
        scanContextValue, hostContextValue, presenceContextValue, schedulerContextValue} = useAppOrchestration();

    return (
        <ComposeProviders providers={[
            [ScanContext, scanContextValue],
            [HostContext, hostContextValue],
            [PresenceContext, presenceContextValue],
            [SchedulerContext, schedulerContextValue],
        ]}>
            <AppShell {...shellProps}>
                <Screen {...screenProps} />
                <LogPanel {...logPanelProps} />
            </AppShell>
        </ComposeProviders>
    );
}

function useSetupMode()
{
    const [setupMode, setSetupMode] = useState(null);

    useEffect(() => {
        fetchWithTimeout("/api/health")
            .then(res => res.json())
            .then(data => setSetupMode(data.mode === "setup"))
            .catch(() => setSetupMode(false));
    }, []);

    return setupMode;
}

function AppRoot()
{
    const setupMode = useSetupMode();
    const appStatus = useAppStatus();
    const [theme, setTheme] = useState(getInitialTheme);
    const [apiKeyModalOpen, setApiKeyModalOpen] = useState(false);

    useEffect(() => {
        setApiKeyPromptListener(() => setApiKeyModalOpen(true));
        return () => setApiKeyPromptListener(null);
    }, []);

    if (setupMode === null)
        return null;

    if (setupMode)
        return <SetupScreen />;

    const appContextValue = {
        ...appStatus,
        theme,
        setTheme,
        availableThemes: AVAILABLE_THEMES,
        openApiKeyPrompt: () => setApiKeyModalOpen(true)
    };

    return (
        <AppContext.Provider value={appContextValue}>
            <AppInner />
            <ApiKeyModal
                open={apiKeyModalOpen}
                onClose={() => setApiKeyModalOpen(false)}
                retry={() => { window.location.reload(); }}
            />
        </AppContext.Provider>
    );
}

export default AppRoot;
