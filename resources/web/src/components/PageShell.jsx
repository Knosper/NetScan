import React from "react";
import { Panel } from "../atoms.jsx";

export function PageShell({ banner, children })
{
    return (
        <section className="page-shell">
            {banner ? <div className="page-head-banner">{banner}</div> : null}
            <div className="page-shell-body">{children}</div>
        </section>
    );
}

export function PageSection({ title, sub, action, children, flush })
{
    return (
        <Panel title={title} sub={sub} action={action} flush={flush}>
            {children}
        </Panel>
    );
}

export function ListRegion({ className = "", children })
{
    const classes = ["list-region", className].filter(Boolean).join(" ");
    return <div className={classes}>{children}</div>;
}
