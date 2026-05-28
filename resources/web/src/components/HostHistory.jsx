import React from "react";
import { toneForScanState } from "../ui/helpers.js";
import { StateNotice } from "../ui/notices.jsx";
import { TimelineRow } from "../atoms.jsx";
import { filterRedundantHistory } from "../ui/hostHistoryUtils.js";
import { portSummary } from "../ui/portSummary.js";
import { historyWhen } from "../ui/hostExposure.js";

export function HostHistory({detail, hideRedundantHistory = false})
{
    if (!detail)
        return null;

    if (!detail.history?.length)
        return <StateNotice kind="empty" message="no host history yet" compact />;

    const history = Array.isArray(detail.history) ? detail.history : [];
    const { entries, hiddenCount } = hideRedundantHistory
        ? filterRedundantHistory(history)
        : { entries: history, hiddenCount: 0 };

    return (
        <div className="history-list host-history-list">
            {hiddenCount > 0 ? (
                <div className="host-history-meta">
                    hiding {hiddenCount} unchanged {hiddenCount === 1 ? "scan" : "scans"}
                </div>
            ) : null}
            {entries.map((entry, index) => {
                const scan = entry.scan || {};
                const ports = entry.ports || [];
                const portsInfo = portSummary(ports);

                return (
                    <TimelineRow
                        key={`${scan.id || "scan"}-${index}`}
                        when={`#${scan.id ?? "-"}`}
                        title={historyWhen(scan)}
                        detail={null}
                        tone={toneForScanState(scan.state, false)}
                    >
                        <div className="host-history-summary">
                            <span className="host-history-open-ports">{ports.length} open ports</span>
                            <span className="host-history-created">
                                {scan.createdAt ? `created ${scan.createdAt}` : "scan timestamp unavailable"}
                            </span>
                            <span className="host-port-summary" title={ports.length > 0 ? portsInfo.fullText : "no open port services"}>
                                {ports.length > 0
                                    ? `${portsInfo.visibleText}${portsInfo.remainingCount > 0 ? ` +${portsInfo.remainingCount} more` : ""}`
                                    : "no open port services"}
                            </span>
                        </div>
                    </TimelineRow>
                );
            })}
        </div>
    );
}
