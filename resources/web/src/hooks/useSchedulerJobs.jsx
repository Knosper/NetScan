import { useState, useCallback, startTransition } from "react";
import { fetchWithTimeout, readOptionalJson } from "../api.js";
import { buildBanner } from "../ui/helpers.js";
import { useAppContext } from "../AppContext.jsx";
import { BANNER_AUTO_HIDE_MS, SHORT_BANNER_AUTO_HIDE_MS } from "../constants/timing.js";

export function useSchedulerJobs() {
    const { setBanner, markBackendUnavailable, clearBackendUnavailable } = useAppContext();
    const [jobs, setJobs] = useState([]);
    const [loading, setLoading] = useState(false);
    const [error, setError] = useState(null);

    const loadJobs = useCallback(async () => {
        setLoading(true);
        setError(null);
        try {
            const response = await fetchWithTimeout("/api/scheduler/jobs");
            if (!response.ok) {
                const payload = await response.json().catch(() => ({}));
                const message = payload?.message || `load failed ${response.status}`;
                setError(message);
                setBanner("scheduler", buildBanner("danger", message), BANNER_AUTO_HIDE_MS);
                return;
            }

            const payload = await response.json();
            clearBackendUnavailable();
            startTransition(() => {
                setJobs(payload.jobs || []);
            });
        } catch (_) {
            markBackendUnavailable();
        } finally {
            setLoading(false);
        }
    }, [setBanner, markBackendUnavailable, clearBackendUnavailable]);

    const createJob = useCallback(async ({ target, ports, hostDiscoveryOnly, intervalSeconds, enabled }) => {
        try {
            const body = { target, interval_seconds: intervalSeconds };
            if (ports && ports.trim())
                body.ports = ports.trim();
            if (typeof hostDiscoveryOnly === "boolean")
                body.host_discovery_only = hostDiscoveryOnly;
            if (typeof enabled === "boolean")
                body.enabled = enabled;

            const response = await fetchWithTimeout("/api/scheduler/jobs", {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify(body)
            });

            if (response.status === 201) {
                clearBackendUnavailable();
                setBanner("scheduler", buildBanner("accent", "job created"), SHORT_BANNER_AUTO_HIDE_MS);
                await loadJobs();
                return true;
            }

            const payload = await readOptionalJson(response);
            setBanner("scheduler", buildBanner("danger", payload?.message || `create failed ${response.status}`), BANNER_AUTO_HIDE_MS);
            return false;
        } catch (_) {
            markBackendUnavailable();
            return false;
        }
    }, [loadJobs, setBanner, markBackendUnavailable, clearBackendUnavailable]);

    const deleteJob = useCallback(async (id) => {
        try {
            const response = await fetchWithTimeout(`/api/scheduler/jobs/${id}`, { method: "DELETE" });

            if (response.status === 204) {
                clearBackendUnavailable();
                setBanner("scheduler", buildBanner("accent", "job deleted"), SHORT_BANNER_AUTO_HIDE_MS);
                await loadJobs();
                return;
            }

            const payload = await readOptionalJson(response);
            setBanner("scheduler", buildBanner("danger", payload?.message || `delete failed ${response.status}`), BANNER_AUTO_HIDE_MS);
        } catch (_) {
            markBackendUnavailable();
        }
    }, [loadJobs, setBanner, markBackendUnavailable, clearBackendUnavailable]);

    const setJobEnabled = useCallback(async (id, enabled) => {
        try {
            const response = await fetchWithTimeout(`/api/scheduler/jobs/${id}`, {
                method: "PATCH",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify({ enabled })
            });

            if (response.ok) {
                clearBackendUnavailable();
                await loadJobs();
                return true;
            }

            const payload = await readOptionalJson(response);
            setBanner("scheduler", buildBanner("danger", payload?.message || `update failed ${response.status}`), BANNER_AUTO_HIDE_MS);
            return false;
        } catch (_) {
            markBackendUnavailable();
            return false;
        }
    }, [loadJobs, setBanner, markBackendUnavailable, clearBackendUnavailable]);

    return {
        jobs,
        loading,
        error,
        loadJobs,
        createJob,
        deleteJob,
        setJobEnabled
    };
}
