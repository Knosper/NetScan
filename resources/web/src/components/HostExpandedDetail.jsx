import React from "react";
import { StateNotice } from "../ui/notices.jsx";
import { Button } from "../atoms.jsx";
import { HostMetaEditor } from "./HostMetaEditor.jsx";
import { HostHistory } from "./HostHistory.jsx";

export function HostExpandedDetail({ data, ui, actions })
{
    const { host, detail, meta } = data;
    const { loading, hideRedundantHistory, showRescan, rescanDisabled } = ui;
    const { onRescan, onPatchMeta, onDeleteMetaField } = actions;
    return (
        <div className="record-detail host-detail">
            <div className="host-detail-head">
                <span>host analysis</span>
                {showRescan ? (
                    <Button type="button" variant="ghost" size="sm" className="host-rescan-button" disabled={rescanDisabled} onClick={onRescan}>
                        {rescanDisabled ? "rescanning…" : "rescan"}
                    </Button>
                ) : null}
            </div>

            {onPatchMeta ? (
                <HostMetaEditor
                    ip={host.ip}
                    meta={meta}
                    onPatch={onPatchMeta}
                    onDeleteField={onDeleteMetaField}
                />
            ) : null}

            {loading ? <StateNotice kind="loading" message="loading host history" compact /> : <HostHistory detail={detail} hideRedundantHistory={hideRedundantHistory} />}
        </div>
    );
}
