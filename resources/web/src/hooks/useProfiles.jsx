import { useState, useCallback, startTransition } from "react";
import { fetchWithTimeout, readOptionalJson } from "../api.js";
import { buildBanner } from "../ui/helpers.js";
import { useAppContext } from "../AppContext.jsx";
import { BANNER_AUTO_HIDE_MS, SHORT_BANNER_AUTO_HIDE_MS } from "../constants/timing.js";

export function useProfiles() {
    const { setBanner, markBackendUnavailable, clearBackendUnavailable } = useAppContext();
    const [profiles, setProfiles] = useState([]);

    const loadProfiles = useCallback(async () => {
        try {
            const response = await fetchWithTimeout("/api/profiles");
            if (!response.ok) {
                setBanner("profiles", buildBanner("danger", `profiles failed ${response.status}`), BANNER_AUTO_HIDE_MS);
                return;
            }

            const payload = await response.json();
            clearBackendUnavailable();
            startTransition(() => {
                setProfiles(payload.profiles || []);
            });
        } catch (_) {
            markBackendUnavailable();
        }
    }, [setBanner, markBackendUnavailable, clearBackendUnavailable]);

    const createProfile = useCallback(async ({ name, target, ports, hostDiscoveryOnly }) => {
        try {
            const body = { name, target, host_discovery_only: hostDiscoveryOnly };
            if (!hostDiscoveryOnly && ports.trim())
                body.ports = ports.trim();

            const response = await fetchWithTimeout("/api/profiles", {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify(body)
            });

            if (response.status === 201) {
                clearBackendUnavailable();
                setBanner("profiles", buildBanner("accent", "profile created"), SHORT_BANNER_AUTO_HIDE_MS);
                await loadProfiles();
                return true;
            }

            const payload = await readOptionalJson(response);
            setBanner("profiles", buildBanner("danger", payload?.message || `create failed ${response.status}`), BANNER_AUTO_HIDE_MS);
            return false;
        } catch (_) {
            markBackendUnavailable();
            return false;
        }
    }, [loadProfiles, setBanner, markBackendUnavailable, clearBackendUnavailable]);

    const updateProfile = useCallback(async (id, name, target, ports, hostDiscoveryOnly) => {
        try {
            const body = { name, target, host_discovery_only: hostDiscoveryOnly };
            if (!hostDiscoveryOnly && ports.trim())
                body.ports = ports.trim();

            const response = await fetchWithTimeout(`/api/profiles/${id}`, {
                method: "PUT",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify(body)
            });

            if (response.ok) {
                clearBackendUnavailable();
                setBanner("profiles", buildBanner("accent", "profile updated"), SHORT_BANNER_AUTO_HIDE_MS);
                await loadProfiles();
                return true;
            }

            const payload = await readOptionalJson(response);
            setBanner("profiles", buildBanner("danger", payload?.message || `update failed ${response.status}`), BANNER_AUTO_HIDE_MS);
            return false;
        } catch (_) {
            markBackendUnavailable();
            return false;
        }
    }, [loadProfiles, setBanner, markBackendUnavailable, clearBackendUnavailable]);

    const deleteProfile = useCallback(async (id) => {
        if (!window.confirm("Delete this profile?"))
            return;

        try {
            const response = await fetchWithTimeout(`/api/profiles/${id}`, { method: "DELETE" });

            if (response.status === 204) {
                clearBackendUnavailable();
                setBanner("profiles", buildBanner("accent", "profile deleted"), SHORT_BANNER_AUTO_HIDE_MS);
                await loadProfiles();
                return;
            }

            const payload = await readOptionalJson(response);
            setBanner("profiles", buildBanner("danger", payload?.message || `delete failed ${response.status}`), BANNER_AUTO_HIDE_MS);
        } catch (_) {
            markBackendUnavailable();
        }
    }, [loadProfiles, setBanner, markBackendUnavailable, clearBackendUnavailable]);

    const runProfile = useCallback(async (id) => {
        try {
            const response = await fetchWithTimeout(`/api/profiles/${id}/run`, { method: "POST" });

            if (response.status === 200) {
                clearBackendUnavailable();
                setBanner("profiles", buildBanner("accent", "scan queued"), SHORT_BANNER_AUTO_HIDE_MS);
                return;
            }

            const payload = await readOptionalJson(response);

            if (response.status === 409) {
                setBanner("profiles", buildBanner("danger", payload?.message || "scan already running"), BANNER_AUTO_HIDE_MS);
                return;
            }

            setBanner("profiles", buildBanner("danger", payload?.message || `run failed ${response.status}`), BANNER_AUTO_HIDE_MS);
        } catch (_) {
            markBackendUnavailable();
        }
    }, [setBanner, markBackendUnavailable, clearBackendUnavailable]);

    return {
        profiles,
        setProfiles,
        loadProfiles,
        createProfile,
        updateProfile,
        deleteProfile,
        runProfile
    };
}
