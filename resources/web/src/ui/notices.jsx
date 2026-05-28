import React from "react";
import { labelForStateKind, stateKindFromBanner, toneForStateKind, toneToClass } from "./helpers.js";

export function StateNotice({kind, message, compact = false})
{
    if (!kind || !message)
        return null;

    const tone = toneForStateKind(kind);

    return (
        <div className={`state-notice state-${kind}${compact ? " is-compact" : ""}`} role="status" aria-live="polite">
            <span className={`dot ${toneToClass(tone)}`} />
            <span className="state-notice-label">{labelForStateKind(kind)}</span>
            <span className="state-notice-text">{message}</span>
        </div>
    );
}

export function InlineBanner({banner})
{
    if (!banner)
        return null;

    const kind = stateKindFromBanner(banner) || "info";

    return (
        <div className={`inline-banner tone-${banner.tone || "info"} state-${kind}`} role="status" aria-live="polite">
            {banner.text}
        </div>
    );
}
