import React from "react";
import { useAppContext } from "../AppContext.jsx";
import { clearApiKey, getApiKey } from "../apiKey.js";
import { ProfileRecord } from "../components/ProfileList.jsx";
import { ProfileForm, HostDiscoveryOnlyField } from "../components/ProfileForm.jsx";
import { PageSection, ListRegion } from "../components/PageShell.jsx";
import { Button, Icon, ListHeaderRow, ListRow, Panel } from "../atoms.jsx";
import { fetchWithTimeout, readOptionalJson } from "../api.js";
import {
  CIDR_PREFIX_OPTIONS,
  createIpTargetRow,
  createSubnetTargetRow
} from "../settingsTargets.js";
import { buildBanner, formatDateTime } from "../ui/helpers.js";
import { InlineBanner, StateNotice } from "../ui/notices.jsx";
import {
  BANNER_AUTO_HIDE_MS,
  SECONDS_PER_HOUR,
  SECONDS_PER_MINUTE
} from "../constants/timing.js";

function AllowedTargetsField({ loading, settings, setSettings, settingsRowErrors, setSettingsRowErrors }) {
  const targets = Array.isArray(settings.user_allowed_targets) ? settings.user_allowed_targets : [];
  const disabled = loading.settings || loading.savingSettings;
  const rowErrors = settingsRowErrors || {};

  function updateTargets(nextTargets) {
    setSettings(previous => ({...previous, user_allowed_targets: nextTargets}));
  }

  function updateRow(index, patch) {
    if (rowErrors[index])
      setSettingsRowErrors(previous => clearRowError(previous, index));
    updateTargets(targets.map((entry, currentIndex) => (
      currentIndex === index ? {...entry, ...patch} : entry
    )));
  }

  function removeRow(index) {
    if (hasAnyRowErrors(rowErrors))
      setSettingsRowErrors({});
    updateTargets(targets.filter((_, currentIndex) => currentIndex !== index));
  }

  function addIpRow() {
    if (hasAnyRowErrors(rowErrors))
      setSettingsRowErrors({});
    updateTargets([...targets, createIpTargetRow()]);
  }

  function addSubnetRow() {
    if (hasAnyRowErrors(rowErrors))
      setSettingsRowErrors({});
    updateTargets([...targets, createSubnetTargetRow()]);
  }

  return (
    <div className="settings-targets-section">
      <AllowedTargetsHeader disabled={disabled} onAddIp={addIpRow} onAddSubnet={addSubnetRow} />

      <div className="settings-target-list">
        {!targets.length ? <AllowedTargetsEmpty /> : null}
        {targets.map((entry, index) => (
          <AllowedTargetRow
            key={`target-${index}`}
            entry={entry}
            index={index}
            disabled={disabled}
            rowError={rowErrors[index] || ""}
            onChange={patch => updateRow(index, patch)}
            onRemove={() => removeRow(index)}
          />
        ))}
      </div>
    </div>
  );
}

function AllowedTargetsHeader({ disabled, onAddIp, onAddSubnet }) {
  return (
    <div className="settings-theme-head">
      <div>
        <div className="settings-theme-title">allowed scan targets</div>
        <div className="settings-theme-sub">
          restricted keys may only start scans within these IPs or CIDR ranges
        </div>
      </div>
      <div className="settings-targets-actions">
        <Button type="button" variant="ghost" size="sm" disabled={disabled} onClick={onAddIp}>
          add ip
        </Button>
        <Button type="button" variant="ghost" size="sm" disabled={disabled} onClick={onAddSubnet}>
          add subnet
        </Button>
      </div>
    </div>
  );
}

function AllowedTargetsEmpty() {
  return (
    <div className="settings-target-empty">
      no targets configured yet. add single IPs like 192.168.1.20 or subnets like
      {" "}192.168.1.0/24.
    </div>
  );
}

function AllowedTargetRow({ entry, index, disabled, rowError, onChange, onRemove }) {
  if (entry?.kind === "cidr") {
    return (
      <div className="settings-target-row">
        <input
          type="text"
          className={targetFieldClassName("settings-target-input", rowError)}
          placeholder="192.168.1.0"
          value={entry.base || ""}
          disabled={disabled}
          title={rowError || ""}
          onChange={event => onChange({base: event.target.value})}
        />
        <select
          className={targetFieldClassName("settings-target-prefix", rowError)}
          value={entry.prefix || "24"}
          disabled={disabled}
          title={rowError || ""}
          onChange={event => onChange({prefix: event.target.value})}
        >
          {CIDR_PREFIX_OPTIONS.map(prefix => (
            <option key={`${index}-${prefix}`} value={prefix}>/{prefix}</option>
          ))}
        </select>
        <Button type="button" variant="ghost" size="sm" tone="danger" disabled={disabled} onClick={onRemove}>
          remove
        </Button>
        <AllowedTargetRowError rowError={rowError} />
      </div>
    );
  }

  return (
    <div className="settings-target-row">
      <input
        type="text"
        className={targetFieldClassName("settings-target-input settings-target-input-wide", rowError)}
        placeholder={entry?.kind === "custom" ? "2001:db8::/64" : "192.168.1.20"}
        value={entry?.value || ""}
        disabled={disabled}
        title={rowError || ""}
        onChange={event => onChange({value: event.target.value})}
      />
      <Button type="button" variant="ghost" size="sm" tone="danger" disabled={disabled} onClick={onRemove}>
        remove
      </Button>
      <AllowedTargetRowError rowError={rowError} />
    </div>
  );
}

function AllowedTargetRowError({ rowError }) {
  return rowError ? <div className="settings-target-row-message">{rowError}</div> : null;
}

function hasAnyRowErrors(rowErrors) {
  return Object.keys(rowErrors).length > 0;
}

function targetFieldClassName(base, rowError) {
  return `field-input ${base}${rowError ? " settings-target-input-error" : ""}`;
}

function SectionSaveRow({ isDirty, loading, className }) {
  if (!isDirty) return null;
  return (
    <div className={`settings-section-save-row${className ? ` ${className}` : ""}`}>
      <span className="settings-section-dirty-badge">● unsaved changes</span>
      <button type="submit" className="button button-text"
        disabled={loading.settings || loading.savingSettings}>
        {loading.savingSettings ? "saving" : "save settings"}
      </button>
    </div>
  );
}

export function SettingsForm({
  loading,
  settings,
  setSettings,
  settingsRowErrors,
  setSettingsRowErrors,
  handleSaveSettings,
  backendUnavailable,
  settingsBanner,
  settingsSaveKind,
  savedSettings
}) {
  const logLevelDirty = savedSettings !== null && settings.log_level !== savedSettings?.log_level;
  const targetsDirty = savedSettings !== null &&
    JSON.stringify(settings.user_allowed_targets) !== JSON.stringify(savedSettings?.user_allowed_targets);

  return (
    <div>
      <form onSubmit={handleSaveSettings}>
        <div className={`field-row settings-log-row${logLevelDirty ? " settings-section-dirty" : ""}`}>
          <label className="field-label settings-log-label" htmlFor="setting-log-level">log level</label>
          <select
            id="setting-log-level"
            className="field-input settings-log-select"
            value={settings.log_level || "info"}
            disabled={loading.settings || loading.savingSettings}
            onChange={event => setSettings(previous => ({...previous, log_level: event.target.value}))}
          >
            <option value="debug">debug</option>
            <option value="info">info</option>
            <option value="warn">warn</option>
            <option value="error">error</option>
          </select>
          <SectionSaveRow isDirty={logLevelDirty} loading={loading} />
        </div>
      </form>

      <form onSubmit={handleSaveSettings}>
        <div className={targetsDirty ? "settings-section-dirty" : ""}>
          <AllowedTargetsField
            loading={loading}
            settings={settings}
            setSettings={setSettings}
            settingsRowErrors={settingsRowErrors}
            setSettingsRowErrors={setSettingsRowErrors}
          />
          <SectionSaveRow isDirty={targetsDirty} loading={loading} className="settings-section-save-row-targets" />
        </div>
      </form>

      {!backendUnavailable && settingsBanner && settingsBanner.tone !== "danger" ? (
        <div className="settings-save-note">
          <StateNotice kind={settingsSaveKind || "completed"} message={settingsBanner.text} compact />
        </div>
      ) : null}
    </div>
  );
}

export function ThemeSettingsSection() {
  const { theme, setTheme, availableThemes = [] } = useAppContext();

  return (
    <div className="settings-theme-section">
      <div className="settings-theme-head">
        <div>
          <div className="settings-theme-title">theme</div>
          <div className="settings-theme-sub">ui-only preference stored in this browser</div>
        </div>
      </div>
      <div className="settings-theme-options" role="radiogroup" aria-label="theme selection">
        {availableThemes.map(option => {
          const active = theme === option.id;
          return (
            <button
              key={option.id}
              type="button"
              role="radio"
              aria-checked={active}
              className={`button button-ghost settings-theme-option${active ? " is-active" : ""}`}
              onClick={() => setTheme(option.id)}
            >
              <span className="settings-theme-option-label">{option.label}</span>
              <span className="settings-theme-option-meta">{option.description}</span>
            </button>
          );
        })}
      </div>
    </div>
  );
}

export function ApiKeySection() {
  const { openApiKeyPrompt } = useAppContext();
  const [hasKey, setHasKey] = React.useState(() => Boolean(getApiKey()));
  const isInsecure = typeof window !== "undefined"
    && window.location?.protocol
    && window.location.protocol !== "https:"
    && window.location.hostname !== "localhost"
    && window.location.hostname !== "127.0.0.1";

  function handleForget() {
    clearApiKey();
    setHasKey(false);
  }

  return (
    <div className="settings-api-key-row">
      <div>
        <div className="settings-theme-title">api key</div>
        <div className="settings-api-key-info">
          {hasKey ? "stored in this browser" : "no key stored — will prompt on next request"}
        </div>
        {isInsecure && hasKey ? (
          <div className="settings-https-warning">
            warning: this connection is not https — the api key is transmitted in plaintext.
          </div>
        ) : null}
      </div>
      <div className="settings-api-key-actions">
        <Button type="button" variant="ghost" size="sm" onClick={() => openApiKeyPrompt?.()}>
          set key
        </Button>
        <Button type="button" variant="ghost" size="sm" onClick={handleForget} disabled={!hasKey}>
          forget key
        </Button>
      </div>
    </div>
  );
}

const SETUP_POLL_INTERVAL_MS = 500;
const SETUP_POLL_TIMEOUT_MS = 30000;

async function waitForSetupServer(url, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    try {
      const res = await fetch(url + "/api/health", { mode: "cors", cache: "no-store" });
      if (res.ok) {
        const data = await res.json().catch(() => ({}));
        if (data && data.mode === "setup")
          return true;
      }
    } catch (_) {
      // server not up yet; keep polling
    }
    await new Promise(resolve => setTimeout(resolve, SETUP_POLL_INTERVAL_MS));
  }
  return false;
}

function RestartOverlay({ status, message }) {
  return (
    <div className="restart-overlay">
      <div className="restart-overlay-card">
        <div className="restart-overlay-status">{status}</div>
        <div className="restart-overlay-message">{message}</div>
      </div>
    </div>
  );
}

function ReRunSetupDialog({ defaultPort, onCancel, onConfirm }) {
  const [port, setPort] = React.useState(String(defaultPort));
  const [submitting, setSubmitting] = React.useState(false);

  const portNum = parseInt(port, 10);
  const portValid = Number.isInteger(portNum) && portNum >= 1 && portNum <= 65535;
  const canSubmit = portValid && !submitting;

  function handleSubmit(event) {
    event.preventDefault();
    if (!canSubmit) return;
    setSubmitting(true);
    onConfirm({ port: portNum });
  }

  return (
    <div className="restart-overlay">
      <form className="restart-overlay-card rerun-setup-dialog" onSubmit={handleSubmit}>
        <div className="restart-overlay-status">Re-run setup wizard</div>
        <div className="restart-overlay-message">
          The current configuration will be replaced. The setup wizard will restart on local loopback only and will be reachable at 127.0.0.1 after the server restarts.
        </div>
        <label className="rerun-setup-field">
          <span className="rerun-setup-label">Setup port</span>
          <input
            className="rerun-setup-input"
            type="number"
            value={port}
            onChange={event => setPort(event.target.value)}
            min="1"
            max="65535"
            disabled={submitting}
            autoFocus
          />
        </label>
        <div className="rerun-setup-actions">
          <button
            type="button"
            className="button button-text button-sm"
            onClick={onCancel}
            disabled={submitting}
          >
            cancel
          </button>
          <button
            type="submit"
            className="button button-text button-sm"
            disabled={!canSubmit}
          >
            continue
          </button>
        </div>
      </form>
    </div>
  );
}

export function SettingsActionRows({ handlePruneClosedPorts, handleShutdown }) {
  const [dialogOpen, setDialogOpen] = React.useState(false);
  const [restartState, setRestartState] = React.useState(null);

  const currentPort = Number(window.location.port) || 8080;

  async function performRestart({ port }) {
    const setupUrl = `http://127.0.0.1:${port}`;
    setDialogOpen(false);
    setRestartState({ status: "Restarting NetScan…", message: "Stopping the current server." });

    try {
      await fetch("/api/settings/setup", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ port })
      });
    } catch (_) {
      // Connection drop is expected; the server is shutting down.
    }

    setRestartState({
      status: "Restarting NetScan…",
      message: `Waiting for the setup server at ${setupUrl}.`
    });

    const ready = await waitForSetupServer(setupUrl, SETUP_POLL_TIMEOUT_MS);
    if (ready) {
      window.location.replace(setupUrl);
      return;
    }

    setRestartState({
      status: "Setup server did not respond in time.",
      message: `Open ${setupUrl} manually to continue setup.`
    });
  }

  return (
    <>
      <div className="settings-shutdown-row">
        <Button
          type="button"
          variant="danger"
          size="sm"
          className="settings-shutdown-button"
          onClick={() => {
            if (window.confirm("Prune all closed ports from the host register?\n\nThis cannot be undone."))
              handlePruneClosedPorts();
          }}
        >
          prune closed ports
        </Button>
      </div>

      <div className="settings-shutdown-row">
        <button
          type="button"
          className="button button-text button-sm settings-shutdown-button"
          onClick={() => setDialogOpen(true)}
          disabled={!!restartState || dialogOpen}
        >
          re-run setup
        </button>
      </div>

      <div className="settings-shutdown-row">
        <button
          type="button"
          className="button button-text button-sm settings-shutdown-button"
          onClick={() => {
            if (window.confirm("Really shut down the server? The application will stop and must be restarted manually."))
              handleShutdown();
          }}
        >
          shutdown server
        </button>
      </div>

      {dialogOpen && (
        <ReRunSetupDialog
          defaultPort={currentPort}
          onCancel={() => setDialogOpen(false)}
          onConfirm={performRestart}
        />
      )}
      {restartState && <RestartOverlay status={restartState.status} message={restartState.message} />}
    </>
  );
}

const PROFILE_PAGE_SIZE = 5;

export function ProfilesSection({
  banners,
  profileForm,
  setProfileForm,
  handleCreateProfile,
  handleCancelProfileEdit,
  profiles,
  profileSearch,
  setProfileSearch,
  filteredProfiles,
  profilePage,
  setProfilePage,
  runProfile,
  startProfileEdit,
  deleteProfile
}) {
  const totalProfilePages = Math.max(1, Math.ceil(filteredProfiles.length / PROFILE_PAGE_SIZE));
  const safePage = Math.min(profilePage, totalProfilePages);
  const visibleProfiles = filteredProfiles.slice((safePage - 1) * PROFILE_PAGE_SIZE, safePage * PROFILE_PAGE_SIZE);

  return (
    <div className="profiles-section">
      <PageSection>
        {banners.profiles ? <InlineBanner banner={banners.profiles} /> : null}

        <Panel title="scan profiles" accent flush>
          <div className="panel-body profiles-form-region">
            <ProfileForm
              profileForm={profileForm}
              setProfileForm={setProfileForm}
              onSubmit={handleCreateProfile}
              onCancel={handleCancelProfileEdit}
            />
          </div>

          {!!profiles.length && (
            <div className="profiles-search-bar">
              <input
                type="text"
                className="field-input"
                placeholder="search profiles..."
                value={profileSearch}
                onChange={event => setProfileSearch(event.target.value)}
              />
            </div>
          )}

          <div className="profile-table">
            {!profiles.length ? <StateNotice kind="empty" message="no profiles" compact /> : null}

            {visibleProfiles.map(profile => (
              <ProfileRecord
                key={profile.id}
                profile={profile}
                onRun={runProfile}
                onEdit={startProfileEdit}
                onDelete={deleteProfile}
              />
            ))}
          </div>

          {totalProfilePages > 1 ? (
            <div className="profiles-pagination">
              <Button
                type="button"
                variant="ghost"
                disabled={safePage <= 1}
                onClick={() => setProfilePage(page => Math.max(1, page - 1))}
              >
                prev
              </Button>
              <span className="profiles-pagination-info">page {safePage} of {totalProfilePages}</span>
              <Button
                type="button"
                variant="ghost"
                disabled={safePage >= totalProfilePages}
                onClick={() => setProfilePage(page => Math.min(totalProfilePages, page + 1))}
              >
                next
              </Button>
            </div>
          ) : null}
        </Panel>
      </PageSection>
    </div>
  );
}

function formatInterval(seconds) {
  if (!seconds || seconds <= 0) return "-";
  if (seconds < SECONDS_PER_MINUTE) return `${seconds}s`;
  if (seconds < SECONDS_PER_HOUR) return `${Math.round(seconds / SECONDS_PER_MINUTE)}m`;
  return `${Math.round(seconds / SECONDS_PER_HOUR)}h`;
}

function SchedulerJobRow({ job, onToggleEnabled, onDelete }) {
  return (
    <ListRow className="scheduler-job-row">
      <div className="scheduler-job-target">
        <strong className="tone-strong">{job.target || "-"}</strong>
        {job.ports ? <span className="tone-muted">{job.ports}</span> : null}
        {job.hostDiscoveryOnly ? <span className="tone-muted">discovery only</span> : null}
      </div>
      <span className="scheduler-job-interval">{formatInterval(job.intervalSeconds)}</span>
      <span className="scheduler-job-ts">{formatDateTime(job.lastRunAt, {fallback: "never"})}</span>
      <span className="scheduler-job-ts">{formatDateTime(job.nextRunAt, {fallback: "never"})}</span>
      <label className="toggle-row scheduler-job-toggle">
        <input
          type="checkbox"
          checked={Boolean(job.enabled)}
          onChange={event => onToggleEnabled(job.id, event.target.checked)}
        />
        <span>{job.enabled ? "enabled" : "disabled"}</span>
      </label>
      <div className="scheduler-job-actions">
        <Button
          type="button"
          variant="ghost"
          size="sm"
          tone="danger"
          onClick={() => {
            if (window.confirm("Delete this scheduled scan?\n\nThis cannot be undone."))
              onDelete(job.id);
          }}
        >
          {Icon.trash()} delete
        </Button>
      </div>
    </ListRow>
  );
}

const EMPTY_SCHEDULER_FORM = { target: "", intervalSeconds: "", ports: "", hostDiscoveryOnly: false, enabled: true };

function SchedulerJobForm({ onSubmit }) {
  const { setBanner } = useAppContext();
  const [form, setForm] = React.useState(EMPTY_SCHEDULER_FORM);
  const [submitting, setSubmitting] = React.useState(false);

  async function handleSubmit(event) {
    event.preventDefault();
    if (submitting) return;
    const intervalSeconds = Number(form.intervalSeconds);
    if (form.intervalSeconds === "" || !Number.isFinite(intervalSeconds) || intervalSeconds < 1) {
      setBanner("scheduler", buildBanner("danger", "interval must be at least 1 second"), BANNER_AUTO_HIDE_MS);
      return;
    }
    setSubmitting(true);
    const ok = await onSubmit({
      target: form.target.trim(),
      intervalSeconds,
      ports: form.hostDiscoveryOnly ? "" : form.ports.trim(),
      hostDiscoveryOnly: form.hostDiscoveryOnly,
      enabled: form.enabled
    });
    setSubmitting(false);
    if (ok) setForm(EMPTY_SCHEDULER_FORM);
  }

  return (
    <form className="profile-form profile-form-grid" onSubmit={handleSubmit}>
      <div className="profile-form-field">
        <label className="profile-form-label">target</label>
        <input
          type="text"
          className="field-input"
          value={form.target}
          placeholder="192.168.1.0/24"
          required
          disabled={submitting}
          onChange={event => setForm(previous => ({ ...previous, target: event.target.value }))}
        />
      </div>
      <div className="profile-form-field">
        <label className="profile-form-label">interval (seconds)</label>
        <input
          type="number"
          className="field-input"
          value={form.intervalSeconds}
          placeholder={String(SECONDS_PER_HOUR)}
          required
          min="1"
          disabled={submitting}
          onChange={event => setForm(previous => ({ ...previous, intervalSeconds: event.target.value }))}
        />
      </div>
      <div className="profile-form-field">
        <label className="profile-form-label">ports</label>
        <input
          type="text"
          className="field-input"
          value={form.ports}
          placeholder="22,80,443"
          disabled={submitting || form.hostDiscoveryOnly}
          onChange={event => setForm(previous => ({ ...previous, ports: event.target.value }))}
        />
      </div>
      <HostDiscoveryOnlyField
        checked={form.hostDiscoveryOnly}
        disabled={submitting}
        onChange={event => setForm(previous => ({
          ...previous,
          hostDiscoveryOnly: event.target.checked,
          ports: event.target.checked ? "" : previous.ports
        }))}
      />
      <div className="profile-form-field profile-form-field-inline">
        <label className="profile-form-checkbox-label">
          <input
            type="checkbox"
            checked={form.enabled}
            disabled={submitting}
            onChange={event => setForm(previous => ({ ...previous, enabled: event.target.checked }))}
          />
          <span>enabled</span>
        </label>
      </div>
      <div className="profile-form-actions">
        <Button type="submit" variant="primary" size="sm" disabled={submitting}>
          {submitting ? "creating…" : "add scheduled scan"}
        </Button>
      </div>
    </form>
  );
}

export function ScheduledScansSection({ banners, schedulerJobs, schedulerLoading, createSchedulerJob, deleteSchedulerJob, setSchedulerJobEnabled }) {
  const schedulerBanner = banners.scheduler || null;

  return (
    <div className="profiles-section">
      <PageSection flush>
        {schedulerBanner ? <InlineBanner banner={schedulerBanner} /> : null}

        <Panel title="scheduled scans" accent flush>
          <div className="panel-body profiles-form-region">
            <SchedulerJobForm onSubmit={createSchedulerJob} />
          </div>

          {schedulerLoading ? (
            <StateNotice kind="loading" message="loading scheduled scans" compact />
          ) : null}

          {!schedulerLoading && !schedulerJobs.length ? (
            <StateNotice kind="empty" message="no scheduled scans yet" compact />
          ) : null}

          {!schedulerLoading && schedulerJobs.length > 0 ? (
            <div className="profile-table">
              <ListHeaderRow className="scheduler-header-row">
                <span>target</span>
                <span>interval</span>
                <span>last run</span>
                <span>next run</span>
                <span>enabled</span>
                <span>actions</span>
              </ListHeaderRow>
              <div className="record-list">
                {schedulerJobs.map(job => (
                  <SchedulerJobRow
                    key={job.id}
                    job={job}
                    onToggleEnabled={setSchedulerJobEnabled}
                    onDelete={deleteSchedulerJob}
                  />
                ))}
              </div>
            </div>
          ) : null}
        </Panel>
      </PageSection>
    </div>
  );
}

const RECONFIGURE_POLL_INTERVAL_MS = 500;
const RECONFIGURE_POLL_TIMEOUT_MS = 15000;
const RECONFIGURE_HEALTH_TIMEOUT_MS = 2000;

export function ReconfigureSection() {
  const [confirmOpen, setConfirmOpen] = React.useState(false);
  const [loading, setLoading] = React.useState(false);
  const [error, setError] = React.useState(null);
  const timeoutRef = React.useRef(null);
  const cancelledRef = React.useRef(false);

  React.useEffect(() => {
    return () => {
      cancelledRef.current = true;
      if (timeoutRef.current) clearTimeout(timeoutRef.current);
    };
  }, []);

  function pollForSetupMode(deadline) {
    if (cancelledRef.current) return;
    if (Date.now() >= deadline) {
      window.location.replace("/");
      return;
    }
    fetchWithTimeout("/api/health", {}, RECONFIGURE_HEALTH_TIMEOUT_MS)
      .then(response => response.ok ? response.json() : null)
      .then(data => {
        if (cancelledRef.current) return;
        if (data && data.mode === "setup") {
          window.location.replace("/");
          return;
        }
        timeoutRef.current = setTimeout(() => pollForSetupMode(deadline), RECONFIGURE_POLL_INTERVAL_MS);
      })
      .catch(() => {
        if (cancelledRef.current) return;
        timeoutRef.current = setTimeout(() => pollForSetupMode(deadline), RECONFIGURE_POLL_INTERVAL_MS);
      });
  }

  async function performReset() {
    setLoading(true);
    setError(null);
    try {
      const response = await fetchWithTimeout("/api/setup/reset", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: "{}"
      });
      if (response.status === 401) {
        setError("Admin key required");
        setLoading(false);
        return;
      }
      if (!response.ok) {
        const data = await readOptionalJson(response);
        setError((data && data.error) || "Reset failed");
        setLoading(false);
        return;
      }
      setConfirmOpen(false);
      pollForSetupMode(Date.now() + RECONFIGURE_POLL_TIMEOUT_MS);
    } catch (_) {
      setError("Could not reach the server");
      setLoading(false);
    }
  }

  function handleCancel() {
    setConfirmOpen(false);
    setError(null);
  }

  return (
    <PageSection title="configuration" flush>
      <ListRegion>
        <div className="settings-shutdown-row">
          <Button
            type="button"
            variant="danger"
            size="sm"
            className="settings-shutdown-button"
            disabled={loading || confirmOpen}
            onClick={() => setConfirmOpen(true)}
          >
            reconfigure netscan
          </Button>
        </div>
        {confirmOpen ? (
          <div className="settings-shutdown-row">
            <div className="setup-warning">
              This will delete conf.ini and restart NetScan in setup mode. Your scan data (netscan.db) is preserved.
            </div>
            {error ? <div className="setup-error">{error}</div> : null}
            <div className="settings-api-key-actions">
              <Button
                type="button"
                variant="ghost"
                size="sm"
                disabled={loading}
                onClick={handleCancel}
              >
                cancel
              </Button>
              <Button
                type="button"
                variant="danger"
                size="sm"
                disabled={loading}
                onClick={performReset}
              >
                {loading ? "restarting…" : "yes, reconfigure"}
              </Button>
            </div>
          </div>
        ) : null}
      </ListRegion>
    </PageSection>
  );
}

function clearRowError(previous, index)
{
  const next = {...(previous || {})};
  delete next[index];
  return next;
}
