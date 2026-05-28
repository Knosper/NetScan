import React from "react";
import { Icon } from "../atoms.jsx";

function formatLogTimestamp(timestamp)
{
    return new Date(timestamp).toISOString().slice(11, 19);
}

export function LogPanel({
    logExpanded,
    logPanelHeight,
    logResizing,
    logEntries,
    handleLogResizeStart,
    handleLogPanelToggle
})
{
    const latestLogEntry = logEntries[0] || null;

    return (
        <section
            className={`shell-log-panel${logExpanded ? " expanded" : " collapsed"}${logResizing ? " resizing" : ""}`}
            style={logExpanded ? {"--shell-log-height": `${logPanelHeight}px`} : undefined}
            aria-label="global event log"
        >
            <div
                className={`shell-log-resize-handle${logExpanded ? " active" : ""}`}
                onMouseDown={handleLogResizeStart}
                aria-hidden="true"
            />
            <div className="shell-log-header">
                <div className="shell-log-summary">
                    <span className="shell-log-title">event log</span>
                    <span className="shell-log-count">{logEntries.length}</span>
                    <span className="shell-log-preview">
                        {latestLogEntry ? latestLogEntry.text : "no user-facing events yet"}
                    </span>
                </div>
                <button
                    type="button"
                    className={`shell-log-toggle${logExpanded ? " expanded" : ""}`}
                    onClick={handleLogPanelToggle}
                    aria-expanded={logExpanded}
                    aria-controls="global-event-log"
                    aria-label={logExpanded ? "collapse event log" : "expand event log"}
                >
                    {Icon.chevron()}
                </button>
            </div>
            <div className="shell-log-body" id="global-event-log" role="log" aria-live="polite">
                {logEntries.length > 0 ? logEntries.map(entry => (
                    <div key={entry.id} className={`shell-log-entry shell-log-entry-${entry.kind}`}>
                        <span className="shell-log-time">{formatLogTimestamp(entry.timestamp)}</span>
                        <span className="shell-log-message">{entry.text}</span>
                    </div>
                )) : (
                    <div className="shell-log-empty">No scan or backend events recorded yet.</div>
                )}
            </div>
        </section>
    );
}
