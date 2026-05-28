import React from "react";
import { Button } from "../atoms.jsx";

export function ProfileRecord({ profile, onRun, onEdit, onDelete, rowRef, isSelected = false })
{
    return (
        <div
            className={`profile-row${isSelected ? " is-selected" : ""}`}
            ref={rowRef}
        >
            <div className="profile-name">{profile.name || "-"}</div>
            <div className="profile-target">{profile.target || "-"}</div>
            <div className="profile-ports">
                {profile.hostDiscoveryOnly
                    ? <span className="profile-ports-discovery">discovery only</span>
                    : <span>{profile.ports || "all ports"}</span>
                }
            </div>
            <div className="profile-row-actions">
                <Button type="button" variant="primary" size="sm" onClick={() => onRun(profile.id)}>run</Button>
                {onEdit ? (
                    <Button type="button" variant="text" size="sm" onClick={() => onEdit(profile)}>edit</Button>
                ) : null}
                <Button type="button" variant="ghost" size="sm" tone="danger" onClick={() => onDelete(profile.id)}>delete</Button>
            </div>
        </div>
    );
}
