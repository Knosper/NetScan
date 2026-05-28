const STATE_PRESENTATION = {
    loading: {tone: "info", label: "loading"},
    empty: {tone: "muted", label: "empty"},
    offline: {tone: "danger", label: "offline"},
    "not-ready": {tone: "warning", label: "not ready"},
    "not-found": {tone: "warning", label: "not found"},
    stale: {tone: "warning", label: "stale"},
    running: {tone: "info", label: "running"},
    completed: {tone: "success", label: "completed"},
    failed: {tone: "danger", label: "failed"},
    changed: {tone: "warning", label: "changed"},
    queued: {tone: "warning", label: "queued"},
    idle: {tone: "muted", label: "idle"},
    info: {tone: "info", label: "info"}
};

export function getHostLabel(ip, hosts)
{
    if (!ip)
        return ip || "";

    const host = getHostMetaSource(ip, hosts);
    const displayName = host?.meta?.displayName;
    return displayName ? `${displayName} (${ip})` : ip;
}

function getHostMetaSource(ip, hosts)
{
    return Array.isArray(hosts)
        ? hosts.find(host => host.ip === ip)
        : null;
}

export function getHostMeta(ip, hosts)
{
    return getHostMetaSource(ip, hosts)?.meta || null;
}

export function normalizeScanState(scan, status)
{
    if (!scan)
        return null;

    return scan.state ? scan : {...scan, state: status};
}

export function formatDateTime(value, {fallback = "—", style = "default"} = {})
{
    if (!value)
        return fallback;

    const date = new Date(value);
    if (Number.isNaN(date.getTime()))
        return String(value);

    if (style === "date")
        return date.toLocaleDateString();

    if (style === "time")
        return date.toLocaleTimeString();

    return date.toLocaleString();
}

export function buildBanner(tone, text)
{
    if (!text)
        return null;

    return {tone, text, timestamp: Date.now()};
}

export function isTerminalScanState(state)
{
    return state === "aborted" || state === "completed" || state === "failed" ||
        state === "dependency_missing";
}

export function toneForHealthStatus(status, backendUnavailable)
{
    if (backendUnavailable)
        return "danger";

    if (status === "ok")
        return "success";

    if (status === "degraded")
        return "warning";

    if (status === "error")
        return "danger";

    return "muted";
}

export function stateKindFromValue(value)
{
    if (!value)
        return "idle";

    const normalized = String(value).toLowerCase();
    const aliases = {
        queued: "queued",
        running: "running",
        completed: "completed",
        failed: "failed",
        aborted: "failed",
        dependency_missing: "failed",
        error: "failed",
        changed: "changed",
        degraded: "changed",
        offline: "offline",
        stale: "stale",
        loading: "loading",
        empty: "empty",
        ok: "completed",
        idle: "idle",
        unknown: "idle"
    };
    return aliases[normalized] || "idle";
}

function stateKindFromScanState(state, backendUnavailable = false)
{
    if (backendUnavailable)
        return "offline";

    return stateKindFromValue(state);
}

export function stateKindFromBanner(banner)
{
    if (!banner?.tone)
        return null;

    const text = String(banner.text || "").toLowerCase();

    if (banner.tone === "danger")
        return "failed";

    if (banner.tone === "warning")
        return "stale";

    if (banner.tone === "success")
        return "completed";

    if (banner.tone === "accent")
    {
        if (text.includes("queued"))
            return "queued";

        if (text.includes("deleting") || text.includes("aborting") || text.includes("queueing") || text.includes("saving"))
            return "running";

        if (text.includes("deleted") || text.includes("aborted") || text.includes("created") ||
            text.includes("saved") || text.includes("completed") || text.includes("restored"))
            return "completed";

        return "running";
    }

    return "running";
}

export function toneForStateKind(kind)
{
    return STATE_PRESENTATION[kind]?.tone || "muted";
}

export function labelForStateKind(kind)
{
    return STATE_PRESENTATION[kind]?.label || STATE_PRESENTATION.idle.label;
}

export function toneForScanState(state, backendUnavailable)
{
    return toneForStateKind(stateKindFromScanState(state, backendUnavailable));
}

export function toneToClass(tone) {
    if (tone === "success") return "ok";
    if (tone === "danger") return "err";
    if (tone === "warning") return "warn";
    if (tone === "info") return "info";
    return "idle";
}
