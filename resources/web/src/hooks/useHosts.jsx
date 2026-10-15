import { useState, useCallback, useEffect, useRef } from "react";
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

function hasHostResultChange(previous, next)
{
    const previousScans = new Map(previous.map(host => [host.ip, host.lastScanId]));
    const nextIps = new Set(next.map(host => host.ip));
    return previous.length !== next.length ||
        next.some(host => !previousScans.has(host.ip) ||
            previousScans.get(host.ip) !== host.lastScanId) ||
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

function applyHostsResult(payload, cache, setters)
{
    const nextHosts = payload.hosts || [];
    const total = Number(payload.total ?? 0);
    const pageSize = Number(payload.pageSize ?? cache.hostPageSize);
    const page = Number(payload.page ?? 1);
    const totalPages = pageSize > 0 ? Math.max(1, Math.ceil(total / pageSize)) : 1;

    setters.setHostPage(page);
    setters.setHostTotalPages(totalPages);
    if (hasHostResultChange(cache.hosts.current, nextHosts)) {
        cache.requests.current.forEach(controller => controller.abort());
        cache.requests.current.clear();
        setters.setHostDetailLoading({});
        setters.setHostDetails({});
        setters.setHostScanDetails({});
    }
    cache.hosts.current = nextHosts;
    setters.setHosts(nextHosts);
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
    const hostSnapshotRef = useRef([]);
    const detailRequestsRef = useRef(new Map());

    const loadHosts = useCallback(async (options = {}) => {
        const { controller, abortFromCaller } = prepareController(hostsRequestRef, options.signal);
        const merged = { q: hostSearch, openPortsOnly: false, limit: hostPageSize, offset: 0, ...options };
        const setters = { setHosts, setHostDetails, setHostDetailLoading, setHostScanDetails, setHostPage, setHostTotalPages };
        const cache = { hosts: hostSnapshotRef, requests: detailRequestsRef, hostPageSize };
        setLoading(true);
        try {
            const result = await executeFetch(buildHostsUrl(merged), controller, hostsRequestRef);
            if (result.stale) return;
            if (result.error !== undefined) {
                setBanner("hosts", buildBanner("danger", `hosts failed ${result.error}`), BANNER_AUTO_HIDE_MS);
                return;
            }
            clearBackendUnavailable();
            applyHostsResult(result.payload, cache, setters);
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
    }, [hostSearch, hostPageSize, setLoading, setBanner, markBackendUnavailable, clearBackendUnavailable, setHosts, setHostDetails, setHostDetailLoading, setHostScanDetails, setHostPage, setHostTotalPages]);

    useEffect(() => () => {
        hostsRequestRef.current?.abort();
        hostsRequestRef.current = null;
        detailRequestsRef.current.forEach(controller => controller.abort());
        detailRequestsRef.current.clear();
    }, []);

    const goToPage = useCallback((targetPage, extras = {}) => {
        const safe = Math.max(1, Math.min(targetPage, hostTotalPages || 1));
        const offset = (safe - 1) * hostPageSize;
        return loadHosts({ ...extras, offset, limit: hostPageSize });
    }, [loadHosts, hostTotalPages, hostPageSize]);

    const loadHostDetail = useCallback(async (ip) => {
        if (!ip || hostDetails[ip] || detailRequestsRef.current.has(ip)) return;

        const controller = new AbortController();
        const isCurrent = () => detailRequestsRef.current.get(ip) === controller;
        detailRequestsRef.current.set(ip, controller);

        setHostDetailLoading(previous => ({ ...previous, [ip]: true }));
        try {
            const response = await fetchWithTimeout(`/api/hosts/${encodeURIComponent(ip)}`, { signal: controller.signal });
            if (!isCurrent()) return;
            if (!response.ok) {
                setBanner("hosts", buildBanner("danger", `host ${ip} failed ${response.status}`), BANNER_AUTO_HIDE_MS);
                return;
            }

            clearBackendUnavailable();
            const payload = await response.json();
            if (isCurrent())
                setHostDetails(previous => ({ ...previous, [ip]: payload }));
        } catch (error) {
            if (isCurrent() && !isAbortError(error)) markBackendUnavailable();
        } finally {
            if (isCurrent()) {
                detailRequestsRef.current.delete(ip);
                setHostDetailLoading(previous => ({ ...previous, [ip]: false }));
            }
        }
    }, [hostDetails, setHostDetailLoading, setBanner, markBackendUnavailable, clearBackendUnavailable, setHostDetails]);

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
