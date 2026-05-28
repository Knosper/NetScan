import { useState, useCallback } from "react";
import { fetchWithTimeout } from "../api.js";
import { buildBanner } from "../ui/helpers.js";
import { BANNER_AUTO_HIDE_MS } from "../constants/timing.js";

export function useHealthAndRefresh({
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
})
{
    const [health, setHealth] = useState({status: "unknown", checks: {}, message: ""});

    const checkHealth = useCallback(async () => {
        try
        {
            const response = await fetchWithTimeout("/api/health");
            if (!response.ok)
            {
                const message = `health failed ${response.status}`;
                setHealth({status: "error", checks: {}, message});
                setBanner("top", buildBanner("danger", message), BANNER_AUTO_HIDE_MS);
                return;
            }

            const payload = await response.json();
            clearBackendUnavailable();
            setHealth(payload);

            if (payload.status === "ok")
                setBanner("top", null);
            else
                setBanner("top", buildBanner("danger", payload.message || `health ${payload.status}`), BANNER_AUTO_HIDE_MS);
        }
        catch (_)
        {
            markBackendUnavailable();
        }
    }, [setBanner, clearBackendUnavailable, markBackendUnavailable]);

    const refreshAll = useCallback(async () => {
        await Promise.all([
            checkHealth(),
            loadDashboard(),
            loadScans(),
            loadHosts(),
            loadSettings(),
            loadProfiles(),
            loadPresenceTrackers(),
            loadSchedulerJobs()
        ]);
    }, [
        checkHealth,
        loadDashboard,
        loadScans,
        loadHosts,
        loadSettings,
        loadProfiles,
        loadPresenceTrackers,
        loadSchedulerJobs
    ]);

    return { health, checkHealth, refreshAll };
}
