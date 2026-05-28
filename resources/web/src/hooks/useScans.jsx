import { useState, useCallback, useEffect, useRef, startTransition } from "react";
import { fetchWithTimeout, isAbortError, readOptionalJson } from "../api.js";
import { buildBanner } from "../ui/helpers.js";
import { useAppContext } from "../AppContext.jsx";
import { BANNER_AUTO_HIDE_MS, SHORT_BANNER_AUTO_HIDE_MS } from "../constants/timing.js";

const TERMINAL_STATES = ["completed", "failed", "aborted", "dependency_missing"];

function markScanAborted(scans, scanId)
{
    const hasScan = scans.some(scan => scan?.id === scanId);
    if (!hasScan)
        return [{id: scanId, state: "aborted"}, ...scans];

    return scans.map(scan => {
        if (scan?.id !== scanId)
            return scan;
        return {...scan, state: "aborted"};
    });
}

function pruneStaleScanResources(previousResources, newScans)
{
    const validScanIds = new Set(newScans.map(s => s.id));
    const updatedResources = {};
    for (const [scanId, resource] of Object.entries(previousResources)) {
        const id = Number(scanId);
        if (!validScanIds.has(id)) continue;
        const scan = newScans.find(s => s.id === id);
        if (scan && TERMINAL_STATES.includes(scan.state))
            updatedResources[id] = resource;
    }
    return updatedResources;
}

function beginRequest(requestRef)
{
    requestRef.current?.abort();
    const controller = new AbortController();
    requestRef.current = controller;
    return controller;
}

function isCurrentRequest(requestRef, controller)
{
    return requestRef.current === controller;
}

function updateScanResourceState(setResourceState, scanId, value)
{
    setResourceState(previous => ({
        ...previous,
        [scanId]: value
    }));
}

function updateManyScanResourceStates(setResourceState, entries)
{
    if (!entries.length)
        return;

    setResourceState(previous => {
        const next = {...previous};
        entries.forEach(({scanId, value}) => {
            next[scanId] = value;
        });
        return next;
    });
}

function scanResourceStateFromResponse(response, payload, options)
{
    if (response.status === 200)
        return {kind: "ready", payload};

    if (response.status === 404) {
        return {
            kind: options.notFoundKind,
            message: payload?.message || options.notFoundMessage
        };
    }

    if (response.status === 409) {
        return {
            kind: options.conflictKind,
            message: payload?.message || options.conflictMessage
        };
    }

    return {
        kind: "error",
        message: payload?.message || `${options.errorPrefix} ${response.status}`
    };
}

async function fetchScanResource(path, controller)
{
    const response = await fetchWithTimeout(path, {signal: controller.signal});
    const payload = await readOptionalJson(response);
    return { response, payload };
}

function resolveMissingScanIds(scanIds, resourceState)
{
    return (Array.isArray(scanIds) ? scanIds : [])
        .map(scanId => Number(scanId || 0))
        .filter(scanId => scanId > 0)
        .filter(scanId => {
            const kind = resourceState[scanId]?.kind;
            return kind !== "loading" && kind !== "ready";
        });
}

async function loadScanResource({
    scanId,
    force = false,
    resourceState,
    setResourceState,
    requestRef,
    path,
    notFoundKind,
    notFoundMessage,
    conflictKind,
    conflictMessage,
    errorPrefix,
    markBackendUnavailable
})
{
    if (!scanId)
        return;

    const currentKind = resourceState[scanId]?.kind;
    if (!force && (currentKind === "loading" || currentKind === "ready"))
        return;

    const controller = beginRequest(requestRef);
    updateScanResourceState(setResourceState, scanId, {kind: "loading"});

    try {
        const { response, payload } = await fetchScanResource(path, controller);
        if (!isCurrentRequest(requestRef, controller))
            return;

        updateScanResourceState(setResourceState, scanId, scanResourceStateFromResponse(response, payload, {
            notFoundKind,
            notFoundMessage,
            conflictKind,
            conflictMessage,
            errorPrefix
        }));
    } catch (error) {
        if (isAbortError(error))
            return;

        markBackendUnavailable();
        if (!isCurrentRequest(requestRef, controller))
            return;

        updateScanResourceState(setResourceState, scanId, {
            kind: "error",
            message: "backend not reachable"
        });
    } finally {
        if (isCurrentRequest(requestRef, controller))
            requestRef.current = null;
    }
}

function useScansState()
{
    const [scans, setScans] = useState([]);
    const [scanDiffs, setScanDiffs] = useState({});
    const [scanTopologies, setScanTopologies] = useState({});
    const [abortingScans, setAbortingScans] = useState({});
    return {
        scans,
        setScans,
        scanDiffs,
        setScanDiffs,
        scanTopologies,
        setScanTopologies,
        abortingScans,
        setAbortingScans
    };
}

function useScansActions({
    setLoading,
    setBanner,
    markBackendUnavailable,
    clearBackendUnavailable,
    scanDiffsRef,
    scanDiffRequestRef,
    setScanDiffs,
    scanTopologiesRef,
    scanTopologyRequestRef,
    scanTopologyBatchRequestRef,
    setScanTopologies,
    setScans,
    setAbortingScans,
    scansRequestRef
})
{
    const loadScans = useCallback(async () => {
        const controller = beginRequest(scansRequestRef);
        setLoading?.(previous => ({...previous, scans: true}));
        try {
            const response = await fetchWithTimeout("/api/scans", {signal: controller.signal});
            if (!isCurrentRequest(scansRequestRef, controller))
                return;

            if (!response.ok) {
                setBanner("scans", buildBanner("danger", `scans failed ${response.status}`), BANNER_AUTO_HIDE_MS);
                return;
            }

            const payload = await response.json();
            if (!isCurrentRequest(scansRequestRef, controller))
                return;

            clearBackendUnavailable();
            const newScans = payload.scans || [];

            startTransition(() => {
                setScans(newScans);
                setScanDiffs(previousDiffs => pruneStaleScanResources(previousDiffs, newScans));
                setScanTopologies(previousTopologies => pruneStaleScanResources(previousTopologies, newScans));
            });
        } catch (error) {
            if (isAbortError(error))
                return;

            markBackendUnavailable();
        } finally {
            if (isCurrentRequest(scansRequestRef, controller)) {
                scansRequestRef.current = null;
                setLoading?.(previous => ({...previous, scans: false}));
            }
        }
    }, [setLoading, setBanner, markBackendUnavailable, clearBackendUnavailable, setScans, setScanDiffs, setScanTopologies, scansRequestRef]);

    const loadScanDiff = useCallback(async (scanId, {force = false} = {}) => {
        await loadScanResource({
            scanId,
            force,
            resourceState: scanDiffsRef.current,
            setResourceState: setScanDiffs,
            requestRef: scanDiffRequestRef,
            path: `/api/scans/${scanId}/diff`,
            notFoundKind: "not-found",
            notFoundMessage: "comparison unavailable",
            conflictKind: "not-diffable",
            conflictMessage: "not diffable",
            errorPrefix: "comparison failed",
            markBackendUnavailable
        });
    }, [scanDiffsRef, scanDiffRequestRef, setScanDiffs, markBackendUnavailable]);

    const updateDiffAcknowledgement = useCallback(async (scanId, key, acknowledged) => {
        if (!scanId || !key?.category || !key?.ip)
            return;

        const method = acknowledged ? "PUT" : "DELETE";
        const label = acknowledged ? "acknowledged" : "reopened";

        try {
            const response = await fetchWithTimeout("/api/scan-diff/acknowledgements", {
                method,
                headers: {"Content-Type": "application/json"},
                body: JSON.stringify(key)
            });
            const payload = await readOptionalJson(response);

            if (!response.ok) {
                setBanner("changes", buildBanner("danger",
                    payload?.message || `acknowledgement failed ${response.status}`), BANNER_AUTO_HIDE_MS);
                return;
            }

            clearBackendUnavailable();
            setBanner("changes", buildBanner("accent", `diff change ${label}`), SHORT_BANNER_AUTO_HIDE_MS);
            await loadScanDiff(scanId, {force: true});
        } catch (_) {
            markBackendUnavailable();
            setBanner("changes", buildBanner("danger", "backend not reachable"), BANNER_AUTO_HIDE_MS);
        }
    }, [setBanner, markBackendUnavailable, clearBackendUnavailable, loadScanDiff]);

    const loadScanTopology = useCallback(async (scanId) => {
        await loadScanResource({
            scanId,
            resourceState: scanTopologiesRef.current,
            setResourceState: setScanTopologies,
            requestRef: scanTopologyRequestRef,
            path: `/api/scans/${scanId}/topology`,
            notFoundKind: "not-found",
            notFoundMessage: "topology unavailable",
            conflictKind: "not-ready",
            conflictMessage: "topology not ready",
            errorPrefix: "topology failed",
            markBackendUnavailable
        });
    }, [scanTopologiesRef, scanTopologyRequestRef, setScanTopologies, markBackendUnavailable]);

    const loadScanTopologies = useCallback(async (scanIds) => {
        const missingScanIds = resolveMissingScanIds(scanIds, scanTopologiesRef.current);
        if (!missingScanIds.length)
            return;

        scanTopologyBatchRequestRef.current?.abort();
        const controller = new AbortController();
        scanTopologyBatchRequestRef.current = controller;

        updateManyScanResourceStates(
            setScanTopologies,
            missingScanIds.map(scanId => ({scanId, value: {kind: "loading"}}))
        );

        try {
            const results = await Promise.all(
                missingScanIds.map(async scanId => {
                    const {response, payload} = await fetchScanResource(
                        `/api/scans/${scanId}/topology`,
                        controller
                    );
                    return {
                        scanId,
                        value: scanResourceStateFromResponse(response, payload, {
                            notFoundKind: "not-found",
                            notFoundMessage: "topology unavailable",
                            conflictKind: "not-ready",
                            conflictMessage: "topology not ready",
                            errorPrefix: "topology failed"
                        })
                    };
                })
            );

            if (scanTopologyBatchRequestRef.current !== controller)
                return;

            updateManyScanResourceStates(setScanTopologies, results);
        } catch (error) {
            if (isAbortError(error))
                return;

            markBackendUnavailable();
            if (scanTopologyBatchRequestRef.current !== controller)
                return;

            updateManyScanResourceStates(
                setScanTopologies,
                missingScanIds.map(scanId => ({
                    scanId,
                    value: {kind: "error", message: "backend not reachable"}
                }))
            );
        } finally {
            if (scanTopologyBatchRequestRef.current === controller)
                scanTopologyBatchRequestRef.current = null;
        }
    }, [scanTopologiesRef, scanTopologyBatchRequestRef, setScanTopologies, markBackendUnavailable]);

    const handleAbortScan = useCallback(async (scanId) => {
        setAbortingScans(previous => ({...previous, [scanId]: true}));
        setBanner("top", buildBanner("accent", `aborting #${scanId}`));

        try {
            const response = await fetchWithTimeout(`/api/scan/${scanId}/abort`, {method: "POST"});
            const payload = await readOptionalJson(response);

            if (!response.ok) {
                setBanner("top", buildBanner("danger",
                    payload?.message || `abort failed ${response.status}`), BANNER_AUTO_HIDE_MS);
                return;
            }

            setBanner("top", buildBanner("accent", `aborted #${scanId}`), SHORT_BANNER_AUTO_HIDE_MS);
            setScans(previous => markScanAborted(previous, scanId));
            await loadScans();
        } catch (_) {
            markBackendUnavailable();
        } finally {
            setAbortingScans(previous => ({...previous, [scanId]: false}));
        }
    }, [setBanner, markBackendUnavailable, loadScans]);

    return {
        loadScans,
        loadScanDiff,
        updateDiffAcknowledgement,
        loadScanTopology,
        loadScanTopologies,
        handleAbortScan
    };
}

export function useScans(setLoading)
{
    const { setBanner, markBackendUnavailable, clearBackendUnavailable } = useAppContext();
    const {
        scans,
        setScans,
        scanDiffs,
        setScanDiffs,
        scanTopologies,
        setScanTopologies,
        abortingScans,
        setAbortingScans
    } = useScansState();
    const scanDiffsRef = useRef(scanDiffs);
    const scanTopologiesRef = useRef(scanTopologies);
    const scansRequestRef = useRef(null);
    const scanDiffRequestRef = useRef(null);
    const scanTopologyRequestRef = useRef(null);
    const scanTopologyBatchRequestRef = useRef(null);
    scanDiffsRef.current = scanDiffs;
    scanTopologiesRef.current = scanTopologies;

    useEffect(() => () => {
        scansRequestRef.current?.abort();
        scanDiffRequestRef.current?.abort();
        scanTopologyRequestRef.current?.abort();
        scanTopologyBatchRequestRef.current?.abort();
        scansRequestRef.current = null;
        scanDiffRequestRef.current = null;
        scanTopologyRequestRef.current = null;
        scanTopologyBatchRequestRef.current = null;
    }, []);

    const {
        loadScans,
        loadScanDiff,
        updateDiffAcknowledgement,
        loadScanTopology,
        loadScanTopologies,
        handleAbortScan
    } = useScansActions({
        setLoading,
        setBanner,
        markBackendUnavailable,
        clearBackendUnavailable,
        scanDiffsRef,
        scanDiffRequestRef,
        setScanDiffs,
        scanTopologiesRef,
        scanTopologyRequestRef,
        scanTopologyBatchRequestRef,
        setScanTopologies,
        setScans,
        setAbortingScans,
        scansRequestRef
    });

    return {
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
    };
}
