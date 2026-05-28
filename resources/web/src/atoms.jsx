import React from "react";
import { stateKindFromValue, toneForStateKind, toTitleCase } from "./ui/helpers.js";
import { getPortRisk } from "./data/portRiskLookup.js";

export function BrandMark() {
    return (
        <div className="brand-mark" aria-hidden="true">
            <i/><i className="on"/><i/>
            <i className="on"/><i className="hot"/><i className="on"/>
            <i/><i className="on"/><i/>
        </div>
    );
}

function icon(node) {
    return (
        <svg viewBox="0 0 16 16" aria-hidden="true" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round">
            {node}
        </svg>
    );
}

export const Icon = {
    dashboard: () => icon(
        <>
            <rect x="2" y="2" width="5" height="5"/>
            <rect x="9" y="2" width="5" height="5"/>
            <rect x="2" y="9" width="5" height="5"/>
            <rect x="9" y="9" width="5" height="5"/>
        </>
    ),
    scan: () => icon(
        <>
            <circle cx="7" cy="7" r="4.5"/>
            <path d="M10.5 10.5L14 14"/>
        </>
    ),
    host: () => icon(
        <>
            <rect x="2" y="3" width="12" height="7"/>
            <path d="M5 13h6M8 10v3"/>
        </>
    ),
    changes: () => icon(
        <>
            <path d="M2.5 4.5h4v4h-4z"/>
            <path d="M9.5 7.5h4v4h-4z"/>
            <path d="M4.5 8.5l2 2 2-2"/>
            <path d="M11.5 6.5l-2-2-2 2"/>
        </>
    ),
    profile: () => icon(<path d="M3 4h10M3 8h10M3 12h6"/>),
    settings: () => icon(
        <>
            <circle cx="8" cy="8" r="2"/>
            <path d="M8 1v2M8 13v2M1 8h2M13 8h2M3 3l1.5 1.5M11.5 11.5L13 13M3 13l1.5-1.5M11.5 4.5L13 3"/>
        </>
    ),
    net: () => icon(
        <>
            <circle cx="8" cy="3" r="1.5"/>
            <circle cx="3" cy="12" r="1.5"/>
            <circle cx="13" cy="12" r="1.5"/>
            <path d="M8 4.5L4 10.5M8 4.5l4 6"/>
        </>
    ),
    health: () => icon(<path d="M2 8h3l1.5-4 3 8 1.5-4H14"/>),
    chevron: ({ dir = "right" } = {}) => icon(<path d="M6 3.5L10.5 8 6 12.5" style={{ transform: dir === "down" ? "rotate(90deg)" : "none" }} />),
    play: () => icon(<path d="M4 3l9 5-9 5z" />),
    stop: () => icon(<rect x="4" y="4" width="8" height="8" />),
    trash: () => icon(
        <>
            <path d="M4 5h8"/>
            <path d="M6 5V4h4v1"/>
            <path d="M5 5l.5 7h5l.5-7"/>
        </>
    ),
    refresh: () => icon(
        <>
            <path d="M2 8a6 6 0 0 1 10.5-4M14 8a6 6 0 0 1-10.5 4"/>
            <path d="M12.5 2v2.5H10M3.5 14v-2.5H6"/>
        </>
    ),
    plus: () => icon(<path d="M8 3v10M3 8h10"/>),
    search: () => icon(
        <>
            <circle cx="7" cy="7" r="4"/>
            <path d="M10 10l3 3"/>
        </>
    ),
    arrow: () => icon(<path d="M3 8h10M9 4l4 4-4 4"/>),
    clock: () => icon(
        <>
            <circle cx="8" cy="8" r="6"/>
            <path d="M8 4.5V8l2.5 1.5"/>
        </>
    ),
    pulse: () => icon(<path d="M1 8h3l2-4 3 8 2-4h4"/>),
    x: () => icon(<path d="M4 4l8 8M12 4l-8 8"/>),
    check: () => icon(<path d="M3 8.5l3 3L13 4"/>),
    filter: () => icon(<path d="M2 3h12l-4.5 6V14L6.5 12V9z"/>),
    download: () => icon(
        <>
            <path d="M8 2v8M4.5 6.5L8 10l3.5-3.5M3 13h10"/>
        </>
    ),
    power: () => icon(
        <>
            <path d="M5 4a5 5 0 1 0 6 0M8 2v6"/>
        </>
    ),
    target: () => icon(
        <>
            <circle cx="8" cy="8" r="6" />
            <circle cx="8" cy="8" r="3" />
            <circle cx="8" cy="8" r="1" fill="currentColor" />
        </>
    )
};

export function StateBadge({ state }) {
    const kind = stateKindFromValue(state);
    const tone = toneForStateKind(kind);

    return (
        <span className={`chip state-badge tone-${tone} state-${kind}`}>
            {kind === "running"
                ? <span className="pulse pulse-xs" />
                : <span className="dot" />}
            {(state || kind).toLowerCase()}
        </span>
    );
}

export function Button({
    variant = "text",
    size,
    tone,
    className = "",
    children,
    ...props
}) {
    const variantClass = {
        text: "button-text",
        primary: "button-primary",
        danger: "button-danger",
        ghost: "button-ghost"
    }[variant] || "button-text";

    const sizeClass = size ? `button-${size}` : "";
    const toneClass = tone ? `tone-${tone}` : "";
    const classes = ["button", variantClass, sizeClass, toneClass, className].filter(Boolean).join(" ");

    return (
        <button {...props} className={classes}>
            {children}
        </button>
    );
}

export function Field({ label, children, className = "" }) {
    return (
        <label className={`filter-field${className ? ` ${className}` : ""}`}>
            {label ? <span className="filter-label">{label}</span> : null}
            {children}
        </label>
    );
}

export function FilterBar({ children, actions, className = "" }) {
    return (
        <div className={`filter-bar${className ? ` ${className}` : ""}`}>
            <div className="filter-bar-fields">{children}</div>
            {actions ? <div className="filter-bar-actions">{actions}</div> : null}
        </div>
    );
}

export function Chip({ tone = "muted", children }) {
    return <span className={`chip tone-${tone}`}>{children}</span>;
}

const SEVERITY_TONE = {
    info:     "info",
    warning:  "warning",
    critical: "danger",
};

export function PortRiskBadge({ port, protocol }) {
    const entry = getPortRisk(port);
    if (!entry) return null;
    const tone = SEVERITY_TONE[entry.severity] || "muted";
    return <Chip tone={tone}>{entry.description}</Chip>;
}

export function ListHeaderRow({ children, className = "" }) {
    return <div className={`list-header-row${className ? ` ${className}` : ""}`}>{children}</div>;
}

export const ListRow = React.forwardRef(function ListRow({ children, className = "", onClick, onKeyDown, role, tabIndex }, ref) {
    return (
        <div ref={ref} className={`list-row${className ? ` ${className}` : ""}`}
            onClick={onClick} onKeyDown={onKeyDown} role={role} tabIndex={tabIndex}>
            {children}
        </div>
    );
});

export function TimelineRow({ when, title, detail, tone = "muted", children }) {
    return (
        <div className={`timeline-row tone-${tone}`}>
            <div className="timeline-when">{when}</div>
            <div className="timeline-main">
                <div className="timeline-title">{title}</div>
                {detail ? <div className="timeline-detail">{detail}</div> : null}
                {children}
            </div>
        </div>
    );
}

export function Panel({ title, sub, action, children, flush, accent }) {
    return (
        <section className={`panel${accent ? " accent" : ""}`}>
            {(title || action) && (
                <div className="panel-head">
                    <div>
                        <h3>{title}</h3>
                        {sub && <div className="sub">{sub}</div>}
                    </div>
                    {action}
                </div>
            )}
            <div className={`panel-body${flush ? " flush" : ""}`}>{children}</div>
        </section>
    );
}
