import React, { useEffect, useRef, useState } from "react";
import { BrandMark } from "../atoms.jsx";
import { fetchWithTimeout } from "../api.js";

function Field({ label, children }) {
    return (
        <div className="setup-field">
            <label className="setup-label">{label}</label>
            {children}
        </div>
    );
}

function SetupForm({ onDone }) {
    const [bindMode, setBindMode] = useState("local");
    const [customHost, setCustomHost] = useState("");
    const [port, setPort] = useState("8080");
    const [generateAdminKey, setGenerateAdminKey] = useState(false);
    const [generateUserKey, setGenerateUserKey] = useState(false);
    const [submitting, setSubmitting] = useState(false);
    const [error, setError] = useState(null);

    useEffect(() => {
        let cancelled = false;
        fetchWithTimeout("/api/setup/defaults")
            .then(res => res.ok ? res.json() : null)
            .then(data => {
                if (cancelled || !data) return;
                if (typeof data.host === "string" && data.host) {
                    if (data.host === "127.0.0.1") {
                        setBindMode("local");
                        setCustomHost("");
                    } else {
                        setBindMode("custom");
                        setCustomHost(data.host);
                    }
                }
                if (Number.isInteger(data.port)) setPort(String(data.port));
            })
            .catch(() => {});
        return () => { cancelled = true; };
    }, []);

    async function handleSubmit(e) {
        e.preventDefault();
        const host = bindMode === "local" ? "127.0.0.1" : customHost.trim();
        const portNum = parseInt(port, 10);
        if (bindMode === "custom" && !host) { setError("Custom address is required"); return; }
        if (!portNum || portNum < 1 || portNum > 65535) { setError("Port must be between 1 and 65535"); return; }

        setSubmitting(true);
        setError(null);
        try {
            const res = await fetchWithTimeout("/api/setup", {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify({ host, port: portNum, generateAdminKey, generateUserKey })
            });
            const data = await res.json();
            if (!res.ok) { setError(data.error || "Setup failed"); setSubmitting(false); return; }
            onDone({ ...data, host, port: portNum });
        } catch (_) {
            setError("Could not reach the server");
            setSubmitting(false);
        }
    }

    return (
        <form className="setup-form" onSubmit={handleSubmit}>
            <Field label="Bind address">
                <label className="setup-checkbox-label">
                    <input
                        type="radio"
                        name="bindMode"
                        value="local"
                        checked={bindMode === "local"}
                        onChange={() => setBindMode("local")}
                        disabled={submitting}
                    />
                    Local only (127.0.0.1)
                </label>
                <label className="setup-checkbox-label">
                    <input
                        type="radio"
                        name="bindMode"
                        value="custom"
                        checked={bindMode === "custom"}
                        onChange={() => setBindMode("custom")}
                        disabled={submitting}
                    />
                    Custom address
                </label>
                {bindMode === "custom" && (
                    <>
                        <input
                            className="setup-input"
                            type="text"
                            value={customHost}
                            onChange={e => setCustomHost(e.target.value)}
                            placeholder="0.0.0.0"
                            disabled={submitting}
                        />
                        <div className="setup-warning">
                            Binding to a custom address exposes NetScan on the network. Ensure you use an API key and secure transport.
                        </div>
                    </>
                )}
            </Field>
            <Field label="Port">
                <input
                    className="setup-input"
                    type="number"
                    value={port}
                    onChange={e => setPort(e.target.value)}
                    min="1"
                    max="65535"
                    disabled={submitting}
                />
            </Field>
            <Field label="Security">
                <label className="setup-checkbox-label">
                    <input
                        type="checkbox"
                        checked={generateAdminKey}
                        onChange={e => setGenerateAdminKey(e.target.checked)}
                        disabled={submitting}
                    />
                    Generate admin API key
                </label>
                <span className="setup-hint">Vollzugriff via REST-API / Skripte.</span>
                <label className="setup-checkbox-label">
                    <input
                        type="checkbox"
                        checked={generateUserKey}
                        onChange={e => setGenerateUserKey(e.target.checked)}
                        disabled={submitting}
                    />
                    Generate restricted user API key
                </label>
                <span className="setup-hint">Read-only access to scan data.</span>
            </Field>
            {error && <div className="setup-error">{error}</div>}
            <button className="setup-submit" type="submit" disabled={submitting}>
                {submitting ? "Saving…" : "Save and start"}
            </button>
        </form>
    );
}

function KeyDisplay({ label, value }) {
    const [copied, setCopied] = useState(false);
    function copy() {
        navigator.clipboard.writeText(value).then(() => {
            setCopied(true);
            setTimeout(() => setCopied(false), 2000);
        });
    }
    return (
        <div className="setup-key-row">
            <span className="setup-key-label">{label}</span>
            <code className="setup-key-value">{value}</code>
            <button className="setup-key-copy" onClick={copy} type="button">
                {copied ? "Copied" : "Copy"}
            </button>
        </div>
    );
}

function SetupDone({ result }) {
    const hasKeys = result.adminKey || result.userKey;
    const [serverState, setServerState] = useState("waiting");
    const startRef = useRef(Date.now());
    const timeoutRef = useRef(null);
    const cancelledRef = useRef(false);

    useEffect(() => {
        cancelledRef.current = false;

        function poll() {
            if (cancelledRef.current) return;
            const elapsed = Date.now() - startRef.current;
            if (elapsed >= 30000) {
                setServerState("timeout");
                return;
            }
            fetchWithTimeout("/api/health", {}, 2000)
                .then(res => res.ok ? res.json() : null)
                .then(data => {
                    if (cancelledRef.current) return;
                    if (data && data.mode === "normal") {
                        setServerState("ready");
                        return;
                    }
                    timeoutRef.current = setTimeout(poll, 500);
                })
                .catch(() => {
                    if (cancelledRef.current) return;
                    timeoutRef.current = setTimeout(poll, 500);
                });
        }

        poll();

        return () => {
            cancelledRef.current = true;
            if (timeoutRef.current) clearTimeout(timeoutRef.current);
        };
    }, []);

    function open() {
        const targetHost = result.host;
        const targetPort = String(result.port);
        const currentHost = window.location.hostname;
        const currentPort = window.location.port;
        const sameHost = targetHost === currentHost;
        const samePort = targetPort === currentPort;
        if (sameHost && samePort) {
            window.location.replace("/");
            return;
        }
        const url = `${window.location.protocol}//${targetHost}:${targetPort}/`;
        window.location.assign(url);
    }

    const buttonDisabled = serverState === "waiting";
    const buttonLabel = serverState === "waiting" ? "Waiting for server…" : "Open NetScan →";

    return (
        <div className="setup-done">
            <div className="setup-done-title">Setup complete</div>
            <p className="setup-done-body">
                NetScan is restarting. The server will be available at the address you configured.
            </p>
            {hasKeys && (
                <div className="setup-keys">
                    <div className="setup-keys-warning">
                        Save these keys now — they will not be shown again.
                    </div>
                    {result.adminKey && <KeyDisplay label="Admin API key" value={result.adminKey} />}
                    {result.userKey && <KeyDisplay label="User API key" value={result.userKey} />}
                </div>
            )}
            {serverState === "timeout" && (
                <div className="setup-warning">
                    Server did not respond in time — click to try anyway.
                </div>
            )}
            <button className="setup-submit" onClick={open} type="button" disabled={buttonDisabled}>
                {buttonLabel}
            </button>
        </div>
    );
}

export function SetupScreen() {
    const [done, setDone] = useState(null);

    return (
        <div className="setup-root">
            <div className="setup-card">
                <div className="setup-header">
                    <BrandMark />
                    <div className="setup-title">NetScan setup</div>
                    <div className="setup-subtitle">
                        Configure where the server listens and optionally generate API keys.
                    </div>
                </div>
                {done ? <SetupDone result={done} /> : <SetupForm onDone={setDone} />}
            </div>
        </div>
    );
}
