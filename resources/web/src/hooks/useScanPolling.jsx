import { startTransition, useRef, useState } from "react";
import { fetchWithTimeout } from "../api.js";
import {
    buildBanner,
    isTerminalScanState,
    normalizeScanState
} from "../ui/helpers.js";
import {
    BANNER_AUTO_HIDE_MS,
    POLL_INTERVAL_ACTIVE_MS,
    POLL_INTERVAL_IDLE_MS
} from "../constants/timing.js";

function isActiveScanState(state)
{
    const normalized = String(state || "").toLowerCase();
    return normalized === "queued" || normalized === "running";
}

function mergeScanProgress(previousScan, nextScan, nextStatus)
{
    if (!nextScan)
        return null;

    if (String(nextStatus || "").toLowerCase() !== "running")
        return nextScan;

    const previousId = previousScan?.id ?? null;
    const nextId = nextScan?.id ?? null;
    const previousProgress = typeof previousScan?.progress === "number"
        ? previousScan.progress
        : null;
    const nextProgress = typeof nextScan?.progress === "number"
        ? nextScan.progress
        : null;

    if (previousId !== nextId)
        return nextScan;

    if (nextProgress === null && previousProgress !== null)
        return {...nextScan, progress: previousProgress};

    if (nextProgress !== null && previousProgress !== null && nextProgress < previousProgress)
        return {...nextScan, progress: previousProgress};

    return nextScan;
}

function applyTerminalScanBanner(nextScan, nextStatus, terminalBanner)
{
    const {refs, setBanner} = terminalBanner;

    if (nextStatus === "completed")
    {
        const bannerKey = `${nextStatus}:${nextScan.id}`;
        if (bannerKey !== refs.completed.current)
        {
            refs.completed.current = bannerKey;
            setBanner("top", buildBanner("accent", `scan #${nextScan.id} completed`), BANNER_AUTO_HIDE_MS);
        }
        return;
    }

    if (nextStatus === "aborted")
    {
        const bannerKey = `aborted:${nextScan.id}`;
        if (bannerKey !== refs.aborted.current)
        {
            refs.aborted.current = bannerKey;
            setBanner("top", buildBanner("danger", `scan #${nextScan.id} aborted`), BANNER_AUTO_HIDE_MS);
        }
        return;
    }

    if (nextStatus === "failed" || nextStatus === "dependency_missing")
    {
        const bannerKey = `failed:${nextScan.id}`;
        if (bannerKey !== refs.failed.current)
        {
            refs.failed.current = bannerKey;
            setBanner("top", buildBanner("danger", nextScan.message || "scan failed"), BANNER_AUTO_HIDE_MS);
        }
    }
}

function applyTerminalReload(nextScan, terminalReload)
{
    const {refs, actions} = terminalReload;

    if (nextScan.id === refs.lastTerminalReloadScanId.current)
        return;

    refs.lastTerminalReloadScanId.current = nextScan.id;
    refs.activePollCount.current = 0;
    actions.loadScanTopology(nextScan.id);
    startTransition(() => {
        actions.loadDashboard();
        actions.loadScans();
        actions.loadHosts();
    });
}

function applyActivePollRefresh(activePollCountRef, loadScans)
{
    activePollCountRef.current = (activePollCountRef.current + 1) % 5;
    if (activePollCountRef.current === 0)
        startTransition(() => loadScans());
}

function buildScanStatusContext(context)
{
    return {
        refs: {
            bannerRefs: context.bannerRefs,
            lastTerminalReloadScanId: context.lastTerminalReloadScanIdRef,
            activePollCount: context.activePollCountRef
        },
        actions: {
            setScanStatus: context.setScanStatus,
            setBanner: context.setBanner,
            loadScanTopology: context.loadScanTopology,
            loadDashboard: context.loadDashboard,
            loadScans: context.loadScans,
            loadHosts: context.loadHosts
        }
    };
}

function applyScanStatusPayload(payload, scanStatusContext)
{
    const { refs, actions } = scanStatusContext;
    const rawNextScan = payload?.scan || null;
    const nextStatus = rawNextScan?.state || payload?.status || "idle";
    const nextScan = normalizeScanState(rawNextScan, nextStatus);

    actions.setScanStatus(previous => ({
        status: nextStatus,
        scan: mergeScanProgress(previous?.scan || null, nextScan, nextStatus)
    }));

    if (!nextScan)
        return POLL_INTERVAL_IDLE_MS;

    applyTerminalScanBanner(nextScan, nextStatus, { refs: refs.bannerRefs.current, setBanner: actions.setBanner });

    if (isTerminalScanState(nextStatus))
        applyTerminalReload(nextScan, { refs, actions });
    else if (isActiveScanState(nextStatus))
        applyActivePollRefresh(refs.activePollCount, actions.loadScans);
    else
        refs.activePollCount.current = 0;

    return isActiveScanState(nextStatus) ? POLL_INTERVAL_ACTIVE_MS : POLL_INTERVAL_IDLE_MS;
}

async function fetchScanStatusPayload({ setBanner, clearBackendUnavailable })
{
    const response = await fetchWithTimeout("/api/scan/status");
    if (!response.ok)
    {
        setBanner("top", buildBanner("danger", `scan status failed ${response.status}`), BANNER_AUTO_HIDE_MS);
        return null;
    }

    const payload = await response.json();
    clearBackendUnavailable();
    return payload;
}

export function buildScanStatusValue({backendUnavailable, scanStatus, currentScanId, currentScanRunning, scanProgress})
{
    if (backendUnavailable)
        return "scan blocked";
    if (scanStatus.status === "queued")
        return "scan queued";
    if (currentScanRunning && currentScanId)
    {
        const progressText = scanProgress !== null ? ` · ${scanProgress}%` : "";
        return `scan #${currentScanId}${progressText}`;
    }
    if (currentScanId)
        return `scan #${currentScanId}`;
    return "scan idle";
}

export function useScanPolling({ setBanner, clearBackendUnavailable, markBackendUnavailable, loadScans, loadHosts, loadDashboard, loadScanTopology })
{
    const [scanStatus, setScanStatus] = useState({status: "idle", scan: null});

    const scanInFlightRef = useRef(false);
    const activePollCountRef = useRef(0);
    const lastTerminalReloadScanIdRef = useRef(0);
    const bannerRefs = useRef({
        completed: {current: ""},
        aborted: {current: ""},
        failed: {current: ""}
    });

    const scanStatusContext = buildScanStatusContext({
        bannerRefs,
        lastTerminalReloadScanIdRef,
        activePollCountRef,
        setScanStatus,
        setBanner,
        loadScanTopology,
        loadDashboard,
        loadScans,
        loadHosts
    });

    async function pollScanStatus()
    {
        if (scanInFlightRef.current)
            return POLL_INTERVAL_ACTIVE_MS;

        scanInFlightRef.current = true;

        try
        {
            const payload = await fetchScanStatusPayload({ setBanner, clearBackendUnavailable });
            if (!payload)
                return POLL_INTERVAL_IDLE_MS;

            return applyScanStatusPayload(payload, scanStatusContext);
        }
        catch (_)
        {
            markBackendUnavailable();
            return POLL_INTERVAL_IDLE_MS;
        }
        finally
        {
            scanInFlightRef.current = false;
        }
    }

    return { scanStatus, setScanStatus, pollScanStatus };
}
