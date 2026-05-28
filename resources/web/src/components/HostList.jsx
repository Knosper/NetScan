import React from "react";
import { Button, ListRow, Chip } from "../atoms.jsx";
import { formatValue, hostExposure } from "../ui/hostExposure.js";
import { HostMetaBadges } from "./HostMetaEditor.jsx";
import { HostExpandedDetail } from "./HostExpandedDetail.jsx";

export { HostMetaBadges } from "./HostMetaEditor.jsx";

function HostSummaryRow({ host, meta, exposure, expanded, onToggle })
{
    return (
        <ListRow>
            <div className="record-row host-row">
                <div className="host-cell host-cell-asset host-col host-col-asset">
                    <div className="host-asset-primary">
                        <strong className="host-asset-ip">{host.ip || "-"}</strong>
                        {meta.displayName ? (
                            <span className="host-asset-display-name">{meta.displayName}</span>
                        ) : null}
                    </div>
                    <div className="host-asset-secondary">
                        <span className="host-asset-name" title={host.name || "unnamed host"}>
                            {host.name || "unnamed host"}
                        </span>
                        <HostMetaBadges meta={meta} />
                    </div>
                </div>

                <div className="host-cell host-cell-scans host-col host-col-scans">
                    <span className="host-cell-value">{host.scanCount ?? 0}</span>
                </div>

                <div className="host-cell host-cell-ports host-col host-col-ports">
                    <span className="host-open-ports-count">{host.openPortCount ?? 0} open</span>
                </div>

                <div className="host-cell host-cell-lastseen host-col host-col-lastseen">
                    <span className="host-cell-value">{formatValue(host.lastSeenAt)}</span>
                </div>

                <div className="host-cell host-cell-cue host-col host-col-exposure" title={exposure.title}>
                    <Chip tone={exposure.tone}>{exposure.label}</Chip>
                </div>

                <div className="host-cell host-cell-actions host-col host-col-detail">
                    <Button type="button" variant="ghost" className={`icon-button icon-button-chevron host-chevron-button${expanded ? " expanded" : ""}`} onClick={onToggle}
                        aria-label={expanded ? "Collapse host" : "Open host"}>
                        <svg viewBox="0 0 16 16" aria-hidden="true" className="chevron">
                            <path d="M6 3.5L10.5 8 6 12.5" />
                        </svg>
                    </Button>
                </div>
            </div>
        </ListRow>
    );
}

export function HostRecord({
    host,
    expanded,
    detail,
    loading,
    onToggle,
    hideRedundantHistory,
    showRescan,
    rescanDisabled,
    onRescan,
    onPatchMeta,
    onDeleteMetaField
})
{
    const exposure = hostExposure(host);
    const meta = host.meta || detail?.host?.meta || detail?.meta || {};

    return (
        <article className="record host-record">
            <HostSummaryRow host={host} meta={meta} exposure={exposure} expanded={expanded} onToggle={onToggle} />

            {expanded ? (
                <HostExpandedDetail
                    data={{ host, detail, meta }}
                    ui={{ loading, hideRedundantHistory, showRescan, rescanDisabled }}
                    actions={{ onRescan, onPatchMeta, onDeleteMetaField }}
                />
            ) : null}
        </article>
    );
}
