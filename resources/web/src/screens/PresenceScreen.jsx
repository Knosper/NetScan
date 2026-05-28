import React from "react";
import { Button, Chip, Icon, ListHeaderRow, ListRow, Panel } from "../atoms.jsx";
import { PageShell, PageSection, ListRegion } from "../components/PageShell.jsx";
import { formatDateTime } from "../ui/helpers.js";
import { InlineBanner, StateNotice } from "../ui/notices.jsx";
import { usePresenceContext } from "../PresenceContext.jsx";
import {
    PRESENCE_DEFAULT_INTERVAL_SECONDS,
    PRESENCE_DEFAULT_TIMEOUT_MS,
    PRESENCE_MIN_INTERVAL_SECONDS,
    PRESENCE_MAX_INTERVAL_SECONDS,
    PRESENCE_MAX_TIMEOUT_MS
} from "../constants/timing.js";

const CHECK_TYPES = ["ping", "tcp", "http"];
const PRESENCE_MAX_TRACKERS = 64;
const EMPTY_FORM = {
    target: "",
    checkType: "ping",
    port: "",
    url: "",
    intervalSeconds: PRESENCE_DEFAULT_INTERVAL_SECONDS,
    timeoutMs: PRESENCE_DEFAULT_TIMEOUT_MS,
    enabled: true
};

function statusTone(status)
{
    if (status === "up")
        return "success";
    if (status === "paused" || status === "down" || status === "timeout")
        return "warning";
    if (status === "error")
        return "danger";
    return "muted";
}

function formatLatency(latency)
{
    if (latency === null || latency === undefined || latency < 0)
        return "-";
    return `${latency} ms`;
}

function trackerTarget(tracker)
{
    if (tracker.checkType === "http" && tracker.url)
        return tracker.url;
    if (tracker.checkType === "tcp" && tracker.port)
        return `${tracker.target}:${tracker.port}`;
    return tracker.target || "-";
}

function normalizeForm(form)
{
    const checkType = CHECK_TYPES.includes(form.checkType) ? form.checkType : "ping";
    return {
        target: form.target.trim(),
        checkType,
        port: checkType === "ping" ? "" : form.port,
        url: checkType === "http" ? form.url.trim() : "",
        intervalSeconds: Number(form.intervalSeconds),
        timeoutMs: Number(form.timeoutMs),
        enabled: Boolean(form.enabled)
    };
}

function validateForm(form)
{
    if (!form.target.trim())
        return "target required";
    if (form.checkType === "tcp" && !String(form.port).trim())
        return "port required for tcp checks";
    if (Number(form.intervalSeconds) < PRESENCE_MIN_INTERVAL_SECONDS)
        return `interval must be at least ${PRESENCE_MIN_INTERVAL_SECONDS} seconds`;
    if (Number(form.intervalSeconds) > PRESENCE_MAX_INTERVAL_SECONDS)
        return `interval must be at most ${PRESENCE_MAX_INTERVAL_SECONDS} seconds`;
    if (Number(form.timeoutMs) < 1 || Number(form.timeoutMs) > PRESENCE_MAX_TIMEOUT_MS)
        return `timeout must be between 1 and ${PRESENCE_MAX_TIMEOUT_MS} ms`;
    return "";
}

function PresenceStatusChip({ result, paused })
{
    const status = paused ? "paused" : (result?.status || "unknown");
    return (
        <Chip tone={statusTone(status)}>
            <span className="presence-status-chip">
                <span aria-hidden="true">{status === "up" ? "ok" : status}</span>
                <span className="presence-status-label">{status}</span>
            </span>
        </Chip>
    );
}

function PresenceFormRow({ id, label, children })
{
    return (
        <div className="field-row">
            <label className="field-label" htmlFor={id}>{label}</label>
            {children}
        </div>
    );
}

function PresenceCheckTypeField({ value, disabled, onChange })
{
    return (
        <PresenceFormRow id="presence-check-type" label="type">
            <select
                id="presence-check-type"
                className="field-input"
                value={value}
                disabled={disabled}
                onChange={onChange}
            >
                <option value="ping">ping</option>
                <option value="tcp">tcp</option>
                <option value="http">http</option>
            </select>
        </PresenceFormRow>
    );
}

function PresenceTargetField({ checkType, value, disabled, onChange })
{
    return (
        <PresenceFormRow id="presence-target" label="target">
            <input
                id="presence-target"
                type="text"
                className="field-input"
                value={value}
                disabled={disabled}
                placeholder={checkType === "http" ? "example.com" : "192.168.1.20"}
                onChange={onChange}
            />
        </PresenceFormRow>
    );
}

function PresencePortField({ checkType, value, disabled, onChange })
{
    if (checkType === "ping")
        return null;

    return (
        <PresenceFormRow id="presence-port" label="port">
            <input
                id="presence-port"
                type="number"
                min="1"
                max="65535"
                className="field-input"
                value={value}
                disabled={disabled}
                placeholder={checkType === "http" ? "80" : "443"}
                onChange={onChange}
            />
        </PresenceFormRow>
    );
}

function PresenceUrlField({ checkType, value, disabled, onChange })
{
    if (checkType !== "http")
        return null;

    return (
        <PresenceFormRow id="presence-url" label="url">
            <input
                id="presence-url"
                type="url"
                className="field-input"
                value={value}
                disabled={disabled}
                placeholder="http://example.com/health"
                onChange={onChange}
            />
        </PresenceFormRow>
    );
}

function PresenceTimingFields({ form, disabled, onUpdateField })
{
    return (
        <div className="presence-form-grid">
            <label className="filter-field">
                <span className="filter-label">interval seconds</span>
                <input
                    type="number"
                    min={PRESENCE_MIN_INTERVAL_SECONDS}
                    max={PRESENCE_MAX_INTERVAL_SECONDS}
                    className="field-input"
                    value={form.intervalSeconds}
                    disabled={disabled}
                    onChange={event => onUpdateField("intervalSeconds", event.target.value)}
                />
            </label>
            <label className="filter-field">
                <span className="filter-label">timeout ms</span>
                <input
                    type="number"
                    min="1"
                    max={PRESENCE_MAX_TIMEOUT_MS}
                    className="field-input"
                    value={form.timeoutMs}
                    disabled={disabled}
                    onChange={event => onUpdateField("timeoutMs", event.target.value)}
                />
            </label>
            <label className="toggle-row presence-enabled-field">
                <input
                    type="checkbox"
                    checked={form.enabled}
                    disabled={disabled}
                    onChange={event => onUpdateField("enabled", event.target.checked)}
                />
                <span>enabled</span>
            </label>
        </div>
    );
}

function PresenceForm({ onSubmit, disabled, trackerCount, banner })
{
    const [form, setForm] = React.useState(EMPTY_FORM);
    const [validationMessage, setValidationMessage] = React.useState("");
    const limitReached = trackerCount >= PRESENCE_MAX_TRACKERS;

    async function handleSubmit(event)
    {
        event.preventDefault();
        const message = validateForm(form);
        setValidationMessage(message);
        if (message)
            return;

        const created = await onSubmit(normalizeForm(form));
        if (created)
            setForm(EMPTY_FORM);
    }

    function updateField(name, value)
    {
        setForm(previous => ({...previous, [name]: value}));
    }

    return (
        <Panel
            title="new tracker"
            sub={`${trackerCount}/${PRESENCE_MAX_TRACKERS} configured`}
            accent
        >
            {banner ? <InlineBanner banner={banner} /> : null}
            {limitReached ? (
                <StateNotice kind="stale" message="tracker limit reached" compact />
            ) : null}
            {validationMessage ? (
                <StateNotice kind="failed" message={validationMessage} compact />
            ) : null}
            <form className="presence-form" onSubmit={handleSubmit}>
                <PresenceCheckTypeField
                    value={form.checkType}
                    disabled={disabled || limitReached}
                    onChange={event => updateField("checkType", event.target.value)}
                />
                <PresenceTargetField
                    checkType={form.checkType}
                    value={form.target}
                    disabled={disabled || limitReached}
                    onChange={event => updateField("target", event.target.value)}
                />
                <PresencePortField
                    checkType={form.checkType}
                    value={form.port}
                    disabled={disabled || limitReached}
                    onChange={event => updateField("port", event.target.value)}
                />
                <PresenceUrlField
                    checkType={form.checkType}
                    value={form.url}
                    disabled={disabled || limitReached}
                    onChange={event => updateField("url", event.target.value)}
                />
                <PresenceTimingFields
                    form={form}
                    disabled={disabled || limitReached}
                    onUpdateField={updateField}
                />
                <div className="form-actions">
                    <Button type="submit" variant="primary" disabled={disabled || limitReached}>
                        {Icon.plus()} add tracker
                    </Button>
                </div>
            </form>
        </Panel>
    );
}

function PresenceTrackerRow({
    tracker,
    checking,
    updating,
    onCheck,
    onTogglePaused,
    onDelete
})
{
    const lastResult = tracker.lastResult || null;
    const paused = !tracker.enabled;
    return (
        <ListRow className="presence-row">
            <div className="presence-target-cell">
                <strong className="tone-strong">{trackerTarget(tracker)}</strong>
                <span className="tone-muted">{tracker.checkType}</span>
                {lastResult?.error ? <span className="presence-error-text">{lastResult.error}</span> : null}
            </div>
            <PresenceStatusChip result={lastResult} paused={paused} />
            <span className="presence-mono">{formatLatency(lastResult?.latencyMs)}</span>
            <span className="presence-mono">{formatDateTime(lastResult?.checkedAt, {fallback: "never"})}</span>
            <div className="presence-actions">
                <Button
                    type="button"
                    variant="ghost"
                    size="sm"
                    tone={paused ? "accent" : "warning"}
                    disabled={updating}
                    onClick={() => onTogglePaused(tracker)}
                >
                    {paused ? "resume" : "pause"}
                </Button>
                <Button type="button" variant="primary" size="sm" disabled={checking} onClick={() => onCheck(tracker.id)}>
                    {Icon.refresh()} {checking ? "checking" : "check"}
                </Button>
                <Button type="button" variant="ghost" size="sm" tone="danger" disabled={updating} onClick={() => onDelete(tracker.id)}>
                    {Icon.trash()} delete
                </Button>
            </div>
        </ListRow>
    );
}

export function PresenceScreen({
    backendUnavailable,
    banners
})
{
    const {
        presenceTrackers, presenceLoading, presenceCheckingTrackerIds, presenceUpdatingTrackerIds,
        createPresenceTracker, updatePresenceTrackerEnabled, deletePresenceTracker, checkPresenceTracker
    } = usePresenceContext();
    const banner = banners.presence || null;

    return (
        <PageShell banner={backendUnavailable ? <StateNotice kind="offline" message="backend not reachable" compact /> : null}>
            <PageSection
                title="presence"
                sub="manual host and service availability checks"
            >
                <PresenceForm
                    onSubmit={createPresenceTracker}
                    disabled={backendUnavailable}
                    trackerCount={presenceTrackers.length}
                    banner={!backendUnavailable && banner ? banner : null}
                />
            </PageSection>

            <PageSection title="trackers" flush>
                {presenceLoading ? <StateNotice kind="loading" message="loading presence trackers" compact /> : null}
                {!presenceLoading && !presenceTrackers.length ? (
                    <StateNotice kind="empty" message="no presence trackers" compact />
                ) : null}
                {!!presenceTrackers.length ? (
                    <ListRegion className="presence-table-region">
                        <ListHeaderRow className="table-header-row presence-header-row">
                            <span>target</span>
                            <span>status</span>
                            <span>latency</span>
                            <span>last checked</span>
                            <span>actions</span>
                        </ListHeaderRow>
                        <div className="record-list">
                            {presenceTrackers.map(tracker => (
                                <PresenceTrackerRow
                                    key={tracker.id}
                                    tracker={tracker}
                                    checking={Boolean(presenceCheckingTrackerIds[tracker.id])}
                                    updating={Boolean(presenceUpdatingTrackerIds[tracker.id])}
                                    onCheck={checkPresenceTracker}
                                    onTogglePaused={item => updatePresenceTrackerEnabled(item, !item.enabled)}
                                    onDelete={deletePresenceTracker}
                                />
                            ))}
                        </div>
                    </ListRegion>
                ) : null}
            </PageSection>
        </PageShell>
    );
}
