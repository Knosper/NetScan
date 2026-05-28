import { useCallback } from "react";
import { fetchWithTimeout, readOptionalJson } from "../api.js";
import { buildBanner } from "../ui/helpers.js";
import { useAppContext } from "../AppContext.jsx";
import { normalizeSettingsForApi } from "../settingsTargets.js";
import {
    BANNER_AUTO_HIDE_MS,
    PRUNE_CLOSED_PORTS_TIMEOUT_MS,
    SETTINGS_SAVE_TIMEOUT_MS,
    SHORT_BANNER_AUTO_HIDE_MS
} from "../constants/timing.js";

const EMPTY_PROFILE_FORM = {
    id: null,
    name: "",
    target: "",
    ports: "",
    hostDiscoveryOnly: false
};

export { EMPTY_PROFILE_FORM };

export function useSettingsHandlers({
    settings,
    setSettingsRowErrors,
    setLoading,
    loadHosts,
    loadSettings
})
{
    const { setBanner, markBackendUnavailable, clearBackendUnavailable } = useAppContext();

    const handleSaveSettings = useCallback(async (event) => {
        event.preventDefault();
        setLoading(previous => ({...previous, savingSettings: true}));
        setSettingsRowErrors?.({});

        try
        {
            const response = await fetchWithTimeout("/api/settings", {
                method: "POST",
                headers: {"Content-Type": "application/json"},
                body: JSON.stringify({settings: normalizeSettingsForApi(settings)})
            }, SETTINGS_SAVE_TIMEOUT_MS);
            const payload = await readOptionalJson(response);

            if (!response.ok)
            {
                const message = String(payload?.message || `save failed ${response.status}`);
                const indexedTargetError = parseAllowedTargetIndexedError(message);
                if (indexedTargetError) {
                    setSettingsRowErrors?.({ [indexedTargetError.index]: indexedTargetError.message });
                }
                setBanner("settings", buildBanner("danger",
                    message), BANNER_AUTO_HIDE_MS);
                return;
            }

            clearBackendUnavailable();
            setSettingsRowErrors?.({});
            setBanner("settings", buildBanner("accent", payload?.message || "settings saved"), SHORT_BANNER_AUTO_HIDE_MS);
            await loadSettings?.();
        }
        catch (_)
        {
            markBackendUnavailable();
        }
        finally
        {
            setLoading(previous => ({...previous, savingSettings: false}));
        }
    }, [settings, setSettingsRowErrors, setLoading, setBanner, markBackendUnavailable, clearBackendUnavailable, loadSettings]);

    const handlePruneClosedPorts = useCallback(async () => {
        try
        {
            const response = await fetchWithTimeout(
                "/api/hosts/ports/closed",
                { method: "DELETE" },
                PRUNE_CLOSED_PORTS_TIMEOUT_MS
            );
            const data = await response.json().catch(() => ({}));
            if (response.ok)
            {
                setBanner("settings", buildBanner("success", `pruned ${data.deleted ?? 0} closed port(s)`), BANNER_AUTO_HIDE_MS);
                loadHosts();
            }
            else
            {
                setBanner("settings", buildBanner("danger", data.error || "prune failed"), BANNER_AUTO_HIDE_MS);
            }
        }
        catch (_)
        {
            setBanner("settings", buildBanner("danger", "prune failed — backend unreachable"), BANNER_AUTO_HIDE_MS);
        }
    }, [setBanner, loadHosts]);

    const handleShutdown = useCallback(async () => {
        if (!window.confirm("Shut down the server?\n\nAny running scan will be aborted."))
            return;

        try
        {
            await fetchWithTimeout("/api/shutdown", {
                method: "POST",
                headers: {"Content-Type": "application/json"}
            });
            setBanner("top", buildBanner("success", "server shutting down..."), BANNER_AUTO_HIDE_MS);
        }
        catch (_)
        {
            setBanner("top", buildBanner("success", "server connection closed"), BANNER_AUTO_HIDE_MS);
            markBackendUnavailable();
        }
    }, [setBanner, markBackendUnavailable]);

    return { handleSaveSettings, handlePruneClosedPorts, handleShutdown };
}

function parseAllowedTargetIndexedError(message)
{
    const match = String(message || "").match(/user_allowed_targets\[(\d+)\]/);
    if (!match)
        return null;

    const index = Number.parseInt(match[1], 10);
    if (!Number.isInteger(index) || index < 0)
        return null;

    return {
        index,
        message
    };
}
