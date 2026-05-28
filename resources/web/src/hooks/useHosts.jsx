import { useState, useCallback, useEffect, useRef, startTransition } from "react";
import { fetchWithTimeout, isAbortError } from "../api.js";
import { buildBanner } from "../ui/helpers.js";
import { useAppContext } from "../AppContext.jsx";
import { BANNER_AUTO_HIDE_MS, HOST_META_SAVE_TIMEOUT_MS } from "../constants/timing.js";

function buildHostsUrl({ q = "", limit, offset, openPortsOnly = false } = {})
{
    const params = new URLSearchParams();
    if (q.trim()) params.append("q", q.trim());
    if (limit != null) params.append("limit", limit);
    if (offset != null) params.append("offset", offset);
    if (openPortsOnly) params.append("openPortsOnly", "true");
    const query = params.toString();
    return query ? `/api/hosts?${query}` : "/api/hosts";
}

function hasStructuralHostChange(previous, next)
{
    const previousIps = new Set(previous.map(host => host.ip));
    const nextIps = new Set(next.map(host => host.ip));
    return previous.length !== next.length ||
        next.some(host => !previousIps.has(host.ip)) ||
        previous.some(host => !nextIps.has(host.ip));
}

function prepareController(hostsRequestRef, callerSignal)
{
    hostsRequestRef.current?.abort();
    const controller = new AbortController();
    hostsRequestRef.current = controller;
    const abortFromCaller = () => controller.abort();

    if (callerSignal?.aborted)
        controller.abort();
    else
        callerSignal?.addEventListener("abort", abortFromCaller, {once: true});

    return { controller, abortFromCaller };
}

async function executeFetch(url, controller, hostsRequestRef)
{
    const response = await fetchWithTimeout(url, {signal: controller.signal});
    if (hostsRequestRef.current !== controller)
        return { stale: true };
    if (!response.ok)
        return { error: response.status };

    const payload = await response.json();
    if (hostsRequestRef.current !== controller)
        return { stale: true };

    return { payload };
}

function applyHostsResult(payload, hostPageSize, setters)
{
    const nextHosts = payload.hosts || [];
    const total = Number(payload.total ?? 0);
    const pageSize = Number(payload.pageSize ?? hostPageSize);
    const page = Number(payload.page ?? 1);
    const totalPages = pageSize > 0 ? Math.max(1, Math.ceil(total / pageSize)) : 1;

    setters.setHostPage(page);
    setters.setHostTotalPages(totalPages);
    startTransition(() => {
        setters.setHosts(previous => {
            if (hasStructuralHostChange(previous, nextHosts)) {
                setters.setHostDetails({});
                setters.setHostScanDetails({});
            }
            return nextHosts;
        });
    });
}

function useHostsState()
{
    const [hosts, setHosts] = useState([]);
    const [loading, setLoading] = useState(false);
    const [expandedHosts, setExpandedHosts] = useState([]);
    const [hostDetails, setHostDetails] = useState({});
    const [hostDetailLoading, setHostDetailLoading] = useState({});
    const [hostScanDetails, setHostScanDetails] = useState({});
    const [hostScanDetailLoading, setHostScanDetailLoading] = useState({});
    const [rescanningHosts, setRescanningHosts] = useState({});
    const [hostSearch, setHostSearch] = useState("");
    const [hideRedundantHistory, setHideRedundantHistory] = useState(true);
    const [hostPage, setHostPage] = useState(1);
    const [hostTotalPages, setHostTotalPages] = useState(1);
    const [hostPageSize] = useState(50);

    return {
        hosts,
        setHosts,
        loading,
        setLoading,
        expandedHosts,
        setExpandedHosts,
        hostDetails,
        setHostDetails,
        hostDetailLoading,
        setHostDetailLoading,
        hostScanDetails,
        setHostScanDetails,
        hostScanDetailLoading,
        setHostScanDetailLoading,
        rescanningHosts,
        setRescanningHosts,
        hostSearch,
        setHostSearch,
        hideRedundantHistory,
        setHideRedundantHistory,
        hostPage,
        setHostPage,
        hostTotalPages,
        setHostTotalPages,
        hostPageSize
    };
}

function useHostsActions(state, app)
{
    const {
        setHosts,
        setLoading,
        hostSearch,
        hostDetails,
        setHostDetails,
        hostDetailLoading,
        setHostDetailLoading,
        hostScanDetails,
        setHostScanDetails,
        hostScanDetailLoading,
        setHostScanDetailLoading,
        setExpandedHosts,
        hostPage,
        setHostPage,
        hostTotalPages,
        setHostTotalPages,
        hostPageSize
    } = state;
    const { setBanner, markBackendUnavailable, clearBackendUnavailable } = app;
    const hostsRequestRef = useRef(null);

    const loadHosts = useCallback(async (options = {}) => {
        const { controller, abortFromCaller } = prepareController(hostsRequestRef, options.signal);
        const merged = { q: hostSearch, openPortsOnly: false, limit: hostPageSize, offset: 0, ...options };
        const setters = { setHosts, setHostDetails, setHostScanDetails, setHostPage, setHostTotalPages };
        setLoading(true);
        try {
            const result = await executeFetch(buildHostsUrl(merged), controller, hostsRequestRef);
            if (result.stale) return;
            if (result.error !== undefined) {
                setBanner("hosts", buildBanner("danger", `hosts failed ${result.error}`), BANNER_AUTO_HIDE_MS);
                return;
            }
            clearBackendUnavailable();
            applyHostsResult(result.payload, hostPageSize, setters);
        } catch (error) {
            if (isAbortError(error))
                return;
            markBackendUnavailable();
        } finally {
            options.signal?.removeEventListener("abort", abortFromCaller);
            if (hostsRequestRef.current === controller) {
                hostsRequestRef.current = null;
                setLoading(false);
            }
        }
    }, [hostSearch, hostPageSize, setLoading, setBanner, markBackendUnavailable, clearBackendUnavailable, setHosts, setHostDetails, setHostScanDetails, setHostPage, setHostTotalPages]);

    useEffect(() => () => {
        hostsRequestRef.current?.abort();
        hostsRequestRef.current = null;
    }, []);

    const goToPage = useCallback((targetPage, extras = {}) => {
        const safe = Math.max(1, Math.min(targetPage, hostTotalPages || 1));
        const offset = (safe - 1) * hostPageSize;
        return loadHosts({ ...extras, offset, limit: hostPageSize });
    }, [loadHosts, hostTotalPages, hostPageSize]);

    const loadHostDetail = useCallback(async (ip) => {
        if (!ip || hostDetails[ip] || hostDetailLoading[ip]) return;

        setHostDetailLoading(previous => ({ ...previous, [ip]: true }));
        try {
            const response = await fetchWithTimeout(`/api/hosts/${encodeURIComponent(ip)}`);
            if (!response.ok) {
                setBanner("hosts", buildBanner("danger", `host ${ip} failed ${response.status}`), BANNER_AUTO_HIDE_MS);
                return;
            }

            clearBackendUnavailable();
            const payload = await response.json();
            setHostDetails(previous => ({ ...previous, [ip]: payload }));
        } catch (_) {
            markBackendUnavailable();
        } finally {
            setHostDetailLoading(previous => ({ ...previous, [ip]: false }));
        }
    }, [hostDetails, hostDetailLoading, setHostDetailLoading, setBanner, markBackendUnavailable, clearBackendUnavailable, setHostDetails]);

    const loadHostScanDetail = useCallback(async (scanId) => {
        if (!scanId || hostScanDetails[scanId] || hostScanDetailLoading[scanId]) return;

        setHostScanDetailLoading(previous => ({ ...previous, [scanId]: true }));
        try {
            const response = await fetchWithTimeout(`/api/scans/${scanId}`);
            if (!response.ok) {
                setBanner("hosts", buildBanner("danger", `scan ${scanId} failed ${response.status}`), BANNER_AUTO_HIDE_MS);
                return;
            }

            clearBackendUnavailable();
            const payload = await response.json();
            setHostScanDetails(previous => ({ ...previous, [scanId]: payload }));
        } catch (_) {
            markBackendUnavailable();
        } finally {
            setHostScanDetailLoading(previous => ({ ...previous, [scanId]: false }));
        }
    }, [hostScanDetails, hostScanDetailLoading, setHostScanDetailLoading, setBanner, markBackendUnavailable, clearBackendUnavailable, setHostScanDetails]);

    const toggleHostExpanded = useCallback((ip) => {
        setExpandedHosts(previous => previous.includes(ip)
            ? previous.filter(value => value !== ip)
            : [...previous, ip]);
    }, [setExpandedHosts]);

    const patchHostMeta = useCallback(async (ip, fields) => {
        try {
            const response = await fetchWithTimeout(`/api/hosts/${encodeURIComponent(ip)}/meta`, {
                method: "PATCH",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify(fields)
            }, HOST_META_SAVE_TIMEOUT_MS);
            if (!response.ok) return { ok: false, status: response.status };

            clearBackendUnavailable();
            const payload = await response.json().catch(() => ({}));
            const nextMeta = payload.meta || { ...(fields || {}) };

            // Update both the expanded host detail cache and the host list row, so the user
            // sees the saved metadata immediately without a manual refresh.
            setHostDetails(previous => {
                if (!previous[ip]) return previous;
                return {
                    ...previous,
                    [ip]: { ...previous[ip], meta: nextMeta }
                };
            });
            setHosts(previous => previous.map(host => (
                host.ip === ip
                    ? { ...host, meta: { ...(host.meta || {}), ...nextMeta } }
                    : host
            )));
            return { ok: true };
        } catch (_) {
            markBackendUnavailable();
            return { ok: false, status: 0 };
        }
    }, [clearBackendUnavailable, markBackendUnavailable, setHostDetails, setHosts]);

    const deleteHostMetaField = useCallback(async (ip, field) => {
        try {
            const response = await fetchWithTimeout(
                `/api/hosts/${encodeURIComponent(ip)}/meta/${encodeURIComponent(field)}`,
                { method: "DELETE" },
                HOST_META_SAVE_TIMEOUT_MS
            );
            if (!response.ok) return { ok: false, status: response.status };

            clearBackendUnavailable();
            setHostDetails(previous => {
                if (!previous[ip]) return previous;
                const nextMeta = { ...(previous[ip].meta || {}) };
                delete nextMeta[field];
                return { ...previous, [ip]: { ...previous[ip], meta: nextMeta } };
            });
            setHosts(previous => previous.map(host => {
                if (host.ip !== ip) return host;
                const nextMeta = { ...(host.meta || {}) };
                delete nextMeta[field];
                return { ...host, meta: nextMeta };
            }));
            return { ok: true };
        } catch (_) {
            markBackendUnavailable();
            return { ok: false, status: 0 };
        }
    }, [clearBackendUnavailable, markBackendUnavailable, setHostDetails, setHosts]);

    return {
        loadHosts,
        goToPage,
        loadHostDetail,
        loadHostScanDetail,
        toggleHostExpanded,
        patchHostMeta,
        deleteHostMetaField
    };
}

export function useHosts()
{
    const app = useAppContext();
    const state = useHostsState();
    const actions = useHostsActions(state, app);
    return { ...state, ...actions };
}
