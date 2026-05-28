import { useState, useCallback, startTransition } from "react";
import { fetchWithTimeout, readOptionalJson } from "../api.js";
import { buildBanner } from "../ui/helpers.js";
import { useAppContext } from "../AppContext.jsx";
import {
    BANNER_AUTO_HIDE_MS,
    PRESENCE_MANUAL_CHECK_TIMEOUT_MS,
    SHORT_BANNER_AUTO_HIDE_MS
} from "../constants/timing.js";

const PRESENCE_API = "/api/presence/trackers";

function errorMessage(payload, fallback)
{
    return payload?.message || fallback;
}

function trackerRequestBody(tracker)
{
    const body = {
        target: tracker.target,
        check_type: tracker.checkType,
        interval_seconds: tracker.intervalSeconds,
        timeout_ms: tracker.timeoutMs,
        enabled: Boolean(tracker.enabled)
    };

    if (tracker.port !== null && tracker.port !== undefined && tracker.port !== "")
        body.port = Number(tracker.port);
    if (tracker.url)
        body.url = tracker.url;

    return body;
}

function usePresenceState()
{
    const [trackers, setTrackers] = useState([]);
    const [loading, setLoading] = useState(true);
    const [checkingTrackerIds, setCheckingTrackerIds] = useState({});
    const [updatingTrackerIds, setUpdatingTrackerIds] = useState({});
    return {
        trackers,
        setTrackers,
        loading,
        setLoading,
        checkingTrackerIds,
        setCheckingTrackerIds,
        updatingTrackerIds,
        setUpdatingTrackerIds
    };
}

function usePresenceActions(state, app)
{
    const {
        setTrackers,
        setLoading,
        setCheckingTrackerIds,
        setUpdatingTrackerIds
    } = state;
    const { setBanner, markBackendUnavailable, clearBackendUnavailable } = app;

    const loadPresenceTrackers = useCallback(async ({ quiet = false } = {}) => {
        if (!quiet) setLoading(true);
        try {
            const response = await fetchWithTimeout(PRESENCE_API);
            const payload = await readOptionalJson(response);
            if (!response.ok) {
                setBanner("presence", buildBanner("danger", errorMessage(payload, `presence failed ${response.status}`)), BANNER_AUTO_HIDE_MS);
                return;
            }

            clearBackendUnavailable();
            startTransition(() => {
                setTrackers(payload?.trackers || []);
            });
        } catch (_) {
            markBackendUnavailable();
        } finally {
            if (!quiet) setLoading(false);
        }
    }, [setLoading, setBanner, markBackendUnavailable, clearBackendUnavailable, setTrackers]);

    const createTracker = useCallback(async (tracker) => {
        try {
            const response = await fetchWithTimeout(PRESENCE_API, {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify(trackerRequestBody(tracker))
            });
            const payload = await readOptionalJson(response);
            if (response.status === 201) {
                clearBackendUnavailable();
                setBanner("presence", buildBanner("accent", "presence tracker created"), SHORT_BANNER_AUTO_HIDE_MS);
                await loadPresenceTrackers();
                return true;
            }

            setBanner("presence", buildBanner("danger", errorMessage(payload, `create failed ${response.status}`)), BANNER_AUTO_HIDE_MS);
            return false;
        } catch (_) {
            markBackendUnavailable();
            return false;
        }
    }, [loadPresenceTrackers, setBanner, markBackendUnavailable, clearBackendUnavailable]);

    const updateTrackerEnabled = useCallback(async (tracker, enabled) => {
        const id = tracker.id;
        setUpdatingTrackerIds(previous => ({ ...previous, [id]: true }));
        try {
            const response = await fetchWithTimeout(`${PRESENCE_API}/${id}`, {
                method: "PUT",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify(trackerRequestBody({ ...tracker, enabled }))
            });
            const payload = await readOptionalJson(response);
            if (response.ok) {
                clearBackendUnavailable();
                setTrackers(previous => previous.map(item =>
                    item.id === id ? (payload?.tracker || { ...item, enabled }) : item));
                setBanner(
                    "presence",
                    buildBanner("accent", enabled ? "presence tracker enabled" : "presence tracker disabled"),
                    SHORT_BANNER_AUTO_HIDE_MS
                );
                return true;
            }

            setBanner("presence", buildBanner("danger", errorMessage(payload, `update failed ${response.status}`)), BANNER_AUTO_HIDE_MS);
            return false;
        } catch (_) {
            markBackendUnavailable();
            return false;
        } finally {
            setUpdatingTrackerIds(previous => ({ ...previous, [id]: false }));
        }
    }, [setUpdatingTrackerIds, clearBackendUnavailable, setTrackers, setBanner, markBackendUnavailable]);

    const deleteTracker = useCallback(async (id) => {
        if (!window.confirm("Delete this presence tracker?")) return;

        setUpdatingTrackerIds(previous => ({ ...previous, [id]: true }));
        try {
            const response = await fetchWithTimeout(`${PRESENCE_API}/${id}`, { method: "DELETE" });
            const payload = await readOptionalJson(response);
            if (response.status === 204) {
                clearBackendUnavailable();
                setTrackers(previous => previous.filter(tracker => tracker.id !== id));
                setBanner("presence", buildBanner("accent", "presence tracker deleted"), SHORT_BANNER_AUTO_HIDE_MS);
                return;
            }

            setBanner("presence", buildBanner("danger", errorMessage(payload, `delete failed ${response.status}`)), BANNER_AUTO_HIDE_MS);
        } catch (_) {
            markBackendUnavailable();
        } finally {
            setUpdatingTrackerIds(previous => ({ ...previous, [id]: false }));
        }
    }, [setUpdatingTrackerIds, clearBackendUnavailable, setTrackers, setBanner, markBackendUnavailable]);

    const checkTracker = useCallback(async (id) => {
        setCheckingTrackerIds(previous => ({ ...previous, [id]: true }));
        try {
            const response = await fetchWithTimeout(`${PRESENCE_API}/${id}/check`, { method: "POST" }, PRESENCE_MANUAL_CHECK_TIMEOUT_MS);
            const payload = await readOptionalJson(response);
            if (response.ok) {
                clearBackendUnavailable();
                setTrackers(previous => previous.map(tracker =>
                    tracker.id === id ? { ...tracker, lastResult: payload?.result || null } : tracker));
                setBanner("presence", buildBanner("accent", "presence check completed"), SHORT_BANNER_AUTO_HIDE_MS);
                await loadPresenceTrackers();
                return;
            }

            setBanner("presence", buildBanner("danger", errorMessage(payload, `check failed ${response.status}`)), BANNER_AUTO_HIDE_MS);
        } catch (_) {
            markBackendUnavailable();
        } finally {
            setCheckingTrackerIds(previous => ({ ...previous, [id]: false }));
        }
    }, [setCheckingTrackerIds, clearBackendUnavailable, setTrackers, loadPresenceTrackers, setBanner, markBackendUnavailable]);

    return {
        loadPresenceTrackers,
        createTracker,
        updateTrackerEnabled,
        deleteTracker,
        checkTracker
    };
}

export function usePresenceTrackers()
{
    const app = useAppContext();
    const state = usePresenceState();
    const actions = usePresenceActions(state, app);
    return { ...state, ...actions };
}
