import React, { useMemo, useState } from "react";
import { PageShell, PageSection, ListRegion } from "../components/PageShell.jsx";
import { Button, Chip, Field, FilterBar, ListHeaderRow, ListRow, PortRiskBadge } from "../atoms.jsx";
import { getPortRisk } from "../data/portRiskLookup.js";
import { StateNotice, InlineBanner } from "../ui/notices.jsx";
import { useHostContext } from "../HostContext.jsx";

function buildServiceRows(hosts, hostDetails)
{
    const rows = [];
    const hostList = Array.isArray(hosts) ? hosts : [];

    for (const host of hostList) {
        const detail = hostDetails[host.ip];
        const history = detail?.history;

        if (Array.isArray(history) && history.length > 0) {
            const latestPorts = Array.isArray(history[0].ports) ? history[0].ports : [];
            if (latestPorts.length === 0)
                continue;
            for (const port of latestPorts) {
                rows.push({
                    key: `${host.ip}:${port.port}`,
                    ip: host.ip,
                    name: host.name || "",
                    port: port.port,
                    service: port.service || "",
                    state: port.state || "open",
                    severity: getPortRisk(port.port)?.severity || null
                });
            }
        } else if (Number(host.openPortCount ?? 0) > 0) {
            rows.push({
                key: `${host.ip}:unknown`,
                ip: host.ip,
                name: host.name || "",
                port: null,
                service: null,
                state: null
            });
        }
    }

    return rows;
}

function matchesRiskFilter(row, riskFilter)
{
    if (riskFilter === "all")
        return true;

    if (riskFilter === "critical")
        return row.severity === "critical";

    if (riskFilter === "warning")
        return row.severity === "warning" || row.severity === "critical";

    return true;
}

const MutedDash = () => <span className="muted">—</span>;

function ServiceRow({ row })
{
    return (
        <ListRow className="service-table-row">
            <span className="service-col-ip tone-accent">{row.ip}</span>
            <span className="service-col-name">{row.name || <MutedDash />}</span>
            <span className="service-col-port">
                {row.port != null
                    ? <><Chip tone="warning">{row.port}</Chip><PortRiskBadge port={row.port} /></>
                    : <MutedDash />}
            </span>
            <span className="service-col-service">
                {row.service || <MutedDash />}
            </span>
        </ListRow>
    );
}

export function ServicesScreen({
    banners,
    loading
}) {
    const { hosts, hostDetails } = useHostContext();
    const [riskFilter, setRiskFilter] = useState("all");
    const detailMap = hostDetails || {};
    const serviceRows = useMemo(() => buildServiceRows(hosts, detailMap), [hosts, detailMap]);
    const filteredRows = useMemo(
        () => serviceRows.filter(row => matchesRiskFilter(row, riskFilter)),
        [serviceRows, riskFilter]
    );
    const activeFilterCount = riskFilter === "all" ? 0 : 1;
    const isLoading = loading?.hosts;
    const emptyMessage = serviceRows.length > 0
        ? "no services match current filters"
        : "no open ports found";

    return (
        <PageShell
            banner={banners?.services ? <InlineBanner banner={banners.services} /> : null}
        >
            <PageSection title="service inventory" sub="open ports across all hosts" flush>
                <FilterBar
                    className="filter-toolbar single-column-toolbar"
                    actions={(
                        <div className="filter-toolbar-actions">
                            <span className="filter-toolbar-count">{activeFilterCount} active</span>
                            <Button
                                type="button"
                                variant="ghost"
                                className="hosts-reset-control"
                                onClick={() => setRiskFilter("all")}
                            >
                                reset
                            </Button>
                        </div>
                    )}
                >
                    <Field label="risk filter" className="filter-toolbar-label">
                        <div className="filter-toolbar-input-wrap filter-toolbar-input-wrap-full">
                            <select
                                aria-label="Filter services by risk"
                                value={riskFilter}
                                onChange={event => setRiskFilter(event.target.value)}
                            >
                                <option value="all">all</option>
                                <option value="warning">warning</option>
                                <option value="critical">critical</option>
                            </select>
                        </div>
                    </Field>
                </FilterBar>

                {isLoading ? (
                    <StateNotice kind="loading" message="loading hosts" compact />
                ) : null}

                {!isLoading && filteredRows.length === 0 ? (
                    <StateNotice kind="empty" message={emptyMessage} compact />
                ) : null}

                {!isLoading && filteredRows.length > 0 ? (
                    <ListRegion className="service-table-region">
                        <ListHeaderRow className="service-header-row">
                            <span>ip</span>
                            <span>hostname</span>
                            <span>port</span>
                            <span>service</span>
                        </ListHeaderRow>
                        <div className="record-list">
                            {filteredRows.map(row => (
                                <ServiceRow key={row.key} row={row} />
                            ))}
                        </div>
                    </ListRegion>
                ) : null}
            </PageSection>
        </PageShell>
    );
}
