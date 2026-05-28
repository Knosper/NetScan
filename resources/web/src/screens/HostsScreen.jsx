import React from "react";
import { HostRecord } from "../components/HostList.jsx";
import { PageShell, ListRegion } from "../components/PageShell.jsx";
import { Button, Field, FilterBar, Icon, Panel } from "../atoms.jsx";
import { InlineBanner, StateNotice } from "../ui/notices.jsx";
import { useHostContext } from "../HostContext.jsx";

export function HostsScreen({
  backendUnavailable,
  loading,
  banners
}) {
  const {
    hosts, expandedHosts, hostDetails, hostDetailLoading, rescanningHosts,
    hostSearch, setHostSearch, showOnlyOpenPorts, setShowOnlyOpenPorts,
    hideRedundantHistory, toggleHostExpanded, handleRescanHost, handlePatchHostMeta,
    handleDeleteHostMetaField, hostPage, hostTotalPages, onGoToPage
  } = useHostContext();
  const activeHostFilterCount = (hostSearch.trim() ? 1 : 0) + (showOnlyOpenPorts ? 1 : 0);

  return (
    <PageShell>
      <div className="hosts-workspace">
        <div className="hosts-section">
          <Panel
            title="host inventory"
            sub="assets and recent visibility"
            accent
          >
            <div className="hosts-panel-stack">
              {!backendUnavailable && banners.hosts ? <InlineBanner banner={banners.hosts} /> : null}

              <FilterBar
                className="filter-toolbar single-column-toolbar"
                actions={(
                  <div className="filter-toolbar-actions">
                    <span className="filter-toolbar-count">{activeHostFilterCount} active</span>
                    <label className="toggle-row hosts-filter-toggle">
                      <input
                        type="checkbox"
                        checked={showOnlyOpenPorts}
                        onChange={event => setShowOnlyOpenPorts(event.target.checked)}
                      />
                      <span>only ports</span>
                    </label>
                  </div>
                )}
              >
                <Field label="asset query" className="filter-toolbar-label">
                  <div className="filter-toolbar-input-wrap filter-toolbar-input-wrap-full">
                    <span className="filter-toolbar-icon">{Icon.search()}</span>
                    <input
                      type="text"
                      placeholder="ip, hostname, tag"
                      aria-label="Search hosts"
                      value={hostSearch}
                      onChange={e => setHostSearch(e.target.value)}
                    />
                  </div>
                </Field>
              </FilterBar>

              {loading.hosts ? <StateNotice kind="loading" message="loading hosts" /> : null}
              {!loading.hosts && !hosts.length ? <StateNotice kind="empty" message="no hosts" /> : null}

              <ListRegion className="host-table-region">
                <div className="record-list">
                  {hosts.map(host => (
                    <HostRecord
                      key={host.ip || host.id}
                      host={host}
                      expanded={expandedHosts.includes(host.ip)}
                      detail={hostDetails[host.ip]}
                      loading={Boolean(hostDetailLoading[host.ip])}
                      onToggle={() => toggleHostExpanded(host.ip)}
                      hideRedundantHistory={hideRedundantHistory}
                      showRescan={host.lastScanId > 0}
                      rescanDisabled={Boolean(rescanningHosts[host.ip])}
                      onRescan={() => handleRescanHost(host.ip, host.lastScanId)}
                      onPatchMeta={handlePatchHostMeta}
                      onDeleteMetaField={handleDeleteHostMetaField}
                    />
                  ))}
                </div>
              </ListRegion>

              {hostTotalPages > 1 ? (
                <div className="hosts-pagination">
                  <Button
                    type="button"
                    variant="ghost"
                    disabled={hostPage <= 1}
                    onClick={() => onGoToPage(hostPage - 1)}
                  >
                    prev
                  </Button>
                  <span className="hosts-pagination-info">page {hostPage} of {hostTotalPages}</span>
                  <Button
                    type="button"
                    variant="ghost"
                    disabled={hostPage >= hostTotalPages}
                    onClick={() => onGoToPage(hostPage + 1)}
                  >
                    next
                  </Button>
                </div>
              ) : null}
            </div>
          </Panel>
        </div>
      </div>
    </PageShell>
  );
}
