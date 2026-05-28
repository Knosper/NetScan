import React from "react";
import { BrandMark, Icon } from "../atoms.jsx";
import { toneToClass } from "../ui/helpers.js";
import { NAV } from "../ui/navigation.js";

function NavItem({ item, screen, go, count, muted = false, mobile = false })
{
    const isActive = screen === item.id;
    const className = mobile
        ? `mobile-nav-item${isActive ? " active" : ""}`
        : `rail-item${isActive ? " active" : ""}${muted ? " rail-item-muted" : ""}`;

    function handleClick() { go(item.id); }
    function handleKeyDown(e) { if (e.key === "Enter" || e.key === " ") { e.preventDefault(); go(item.id); } }

    return (
        <div
            className={className}
            onClick={handleClick}
            onKeyDown={handleKeyDown}
            tabIndex={0}
            role="button"
            title={muted ? "Services Workspace (preview / limited)" : item.label}
            aria-current={isActive ? "page" : undefined}
        >
            <item.icon />
            {!mobile && <span>{item.label}</span>}
            {!mobile && <span className="rail-count">{count}</span>}
        </div>
    );
}

function TopBar({ navigation, counts, status })
{
    const { isDashboardScreen, go, current } = navigation || {};
    const { hostsCount, totalPorts, navCounts, scansCount } = counts || {};
    const { backendTone, nmapTone, dbTone, currentScanActive, scanStatusValue } = status || {};

    function handleDashboardClick() { go("dashboard"); }

    return (
        <header className="topbar">
            <button
                type="button"
                className="topbar-brand topbar-brand-button"
                onClick={handleDashboardClick}
                aria-label="go to dashboard"
            ><BrandMark /><div><div className="brand-name">NetScan</div></div></button>
            <div className="topbar-breadcrumb">{isDashboardScreen ? <span className="here">dashboard</span> : <><button type="button" className="button button-ghost button-sm breadcrumb-root" onClick={handleDashboardClick}>dashboard</button><span className="sep">&#9658;</span><span className="here">{current?.label}</span></>}</div>
            <div className="topbar-kpi"><span className="kpi-inline">hosts <strong>{hostsCount}</strong></span><span className="kpi-inline">ports <strong>{totalPorts}</strong></span><span className="kpi-inline">services <strong>{navCounts.dashboard}</strong></span><span className="kpi-inline">scans <strong>{scansCount}</strong></span></div>
            <div className="topbar-meta"><div className="meta-item"><span className={`dot ${toneToClass(backendTone)}`} /><span className="meta-label">backend</span></div><div className="meta-item"><span className={`dot ${toneToClass(nmapTone)}`} /><span className="meta-label">nmap</span></div><div className="meta-item"><span className={`dot ${toneToClass(dbTone)}`} /><span className="meta-label">db</span></div>{currentScanActive && <div className="meta-item"><span className="pulse pulse-sm" /><span className="meta-label">live</span><span className="meta-value">{scanStatusValue.replace(/^scan\s/, "")}</span></div>}</div>
        </header>
    );
}

function RailNav({ screen, go, navCounts, appVersion, hostUrl })
{
    return (
        <nav className="rail" aria-label="main navigation">
            <div className="rail-group"><div className="rail-label">workspace</div>{NAV.map(item => <NavItem key={item.id} item={item} screen={screen} go={go} count={navCounts[item.id]} muted={item.id === "services"} />)}</div>
            <div className="rail-group"><div className="rail-label">browser</div><div className="rail-item rail-item-static" title="Browser URL; this can differ from the configured server port when a container or IDE forwards ports."><Icon.net /><span>{hostUrl}</span></div></div>
            <div className="rail-spacer" />
            <div className="rail-footer"><div className="kv"><span>v{appVersion}</span></div></div>
        </nav>
    );
}

function MobileNav({ screen, go })
{
    return (
        <nav className="mobile-nav">
            {NAV.map(item => <NavItem key={item.id} item={item} screen={screen} go={go} mobile />)}
        </nav>
    );
}

function StatusBar({ scan, system })
{
    const { scanTone, backendUnavailable, scanStatus, currentScanActive, scanStatusValue } = scan;
    const { pollInterval, utcTime } = system;
    return (
        <footer className="statusbar" role="status" aria-live="polite">
            <span className="statusbar-item"><span className={`dot ${toneToClass(scanTone)}`} /><strong className="statusbar-value">{backendUnavailable ? "blocked" : scanStatus.status}</strong></span>
            {currentScanActive && <span className="statusbar-item"><span className="pulse pulse-xs" /><span className="statusbar-value">{scanStatusValue.replace(/^scan\s/, "")}</span></span>}
            <span className="statusbar-item push"><span className="statusbar-value">poll {pollInterval}</span></span>
            <span className="statusbar-item"><span className="statusbar-value">{utcTime}</span></span>
        </footer>
    );
}

export function AppShell(props)
{
    const { screen, go, children, theme, navigation, counts, status } = props;
    return (
        <div className="app" data-density="compact" data-theme={theme || "dark"}>
            <TopBar navigation={navigation} counts={counts} status={status} />
            <RailNav screen={screen} go={go} navCounts={props.navCounts} appVersion={props.appVersion} hostUrl={window.location.host} />
            <MobileNav screen={screen} go={go} />
            <main className="main">{children}</main>
            <StatusBar scan={props.scan} system={props.system} />
        </div>
    );
}
