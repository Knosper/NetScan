import { useCallback } from "react";
import { fetchWithTimeout, readOptionalJson } from "../api.js";
import { buildBanner, normalizeScanState } from "../ui/helpers.js";
import { useAppContext } from "../AppContext.jsx";
import { BANNER_AUTO_HIDE_MS, SHORT_BANNER_AUTO_HIDE_MS } from "../constants/timing.js";

function buildRescanRequest(ip, scanPayload)
{
    const body = {
        target: ip,
        host_discovery_only: Boolean(scanPayload.hostDiscoveryOnly)
    };

    if (!scanPayload.hostDiscoveryOnly && scanPayload.requestedPorts)
        body.ports = scanPayload.requestedPorts;

    return body;
}

export function useScanHandlers({
    form,
    setScanStatus,
    setRescanningHosts,
    setLoading,
    loadDashboard,
    loadScans,
    loadHosts
})
{
    const { setBanner, markBackendUnavailable } = useAppContext();

    const submitScanStart = useCallback(async (requestBody, bannerDurationMs) => {
        const response = await fetchWithTimeout("/api/scan/start", {
            method: "POST",
            headers: {"Content-Type": "application/json"},
            body: JSON.stringify(requestBody)
        });
        const payload = await readOptionalJson(response);

        if (response.status === 202)
        {
            const nextStatus = payload?.scan?.state || "queued";
            setScanStatus({
                status: nextStatus,
                scan: normalizeScanState(payload?.scan || null, nextStatus)
            });
            setBanner("top", buildBanner("accent", "scan queued"), bannerDurationMs);
            await Promise.all([loadDashboard(), loadScans(), loadHosts()]);
            return;
        }

        if (response.status === 409)
        {
            setBanner("top", buildBanner("danger", payload?.message || "scan already running"), BANNER_AUTO_HIDE_MS);
            return;
        }

        setBanner("top", buildBanner("danger", payload?.message || `start failed ${response.status}`), BANNER_AUTO_HIDE_MS);
    }, [setScanStatus, setBanner, loadDashboard, loadScans, loadHosts]);

    const handleStartScan = useCallback(async (event, overrideTarget) => {
        event.preventDefault();

        const target = (overrideTarget !== undefined ? overrideTarget : form.target).trim();
        if (!target)
        {
            setBanner("top", buildBanner("danger", "target required"), BANNER_AUTO_HIDE_MS);
            return;
        }

        setLoading(previous => ({...previous, starting: true}));
        setBanner("top", buildBanner("accent", "queueing scan"));

        try
        {
            const requestBody = {
                target,
                host_discovery_only: form.hostDiscoveryOnly
            };

            if (!form.hostDiscoveryOnly && form.ports.trim())
                requestBody.ports = form.ports.trim();

            await submitScanStart(requestBody);
        }
        catch (_)
        {
            markBackendUnavailable();
        }
        finally
        {
            setLoading(previous => ({...previous, starting: false}));
        }
    }, [form, submitScanStart, setBanner, setLoading, markBackendUnavailable]);

    const handleRescanHost = useCallback(async (ip, lastScanId) => {
        if (!lastScanId)
            return;

        setRescanningHosts(previous => ({...previous, [ip]: true}));

        try
        {
            const scanResponse = await fetchWithTimeout(`/api/scans/${lastScanId}`);
            let scanPayload = await readOptionalJson(scanResponse);

            if (!scanResponse.ok || !scanPayload)
                scanPayload = { hostDiscoveryOnly: false, requestedPorts: null };

            await submitScanStart(buildRescanRequest(ip, scanPayload), SHORT_BANNER_AUTO_HIDE_MS);
        }
        catch (_)
        {
            markBackendUnavailable();
        }
        finally
        {
            setRescanningHosts(previous => ({...previous, [ip]: false}));
        }
    }, [submitScanStart, setRescanningHosts, markBackendUnavailable]);

    return { handleStartScan, handleRescanHost, submitScanStart };
}
