#include "db/dashboard_repository.hpp"
#include "db/sqlite_helpers.hpp"
#include <sqlite3.h>
#include <string>

template <typename Row, typename RowFn>
static std::vector<Row> read_rows(sqlite3* h, const char* sql, RowFn row_fn)
{
    std::vector<Row> rows;
    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (!stmt.ptr)
        return rows;
    while (sqlite3_step(stmt.ptr) == SQLITE_ROW)
        rows.push_back(row_fn(stmt.ptr));
    return rows;
}

static DashboardScanRow scan_row_from_stmt(sqlite3_stmt* stmt)
{
    DashboardScanRow row;
    row.id = sqlite3_column_int(stmt, 0);
    row.target = col_text(stmt, 1);
    row.status = col_text(stmt, 2);
    row.host_discovery_only = sqlite3_column_int(stmt, 3) != 0;
    row.created_at = col_text(stmt, 4);
    row.host_count = sqlite3_column_int(stmt, 5);
    row.port_count = sqlite3_column_int(stmt, 6);
    return row;
}

static DashboardHostRow host_row_from_stmt(sqlite3_stmt* stmt)
{
    DashboardHostRow row;
    row.id = sqlite3_column_int(stmt, 0);
    row.ip = col_text(stmt, 1);
    row.name = col_text(stmt, 2);
    return row;
}

static DashboardPortRow port_row_from_stmt(sqlite3_stmt* stmt)
{
    DashboardPortRow row;
    row.id = sqlite3_column_int(stmt, 0);
    row.host = col_text(stmt, 1);
    row.port = sqlite3_column_int(stmt, 2);
    row.service = col_text(stmt, 3);
    return row;
}

static DashboardStats read_dashboard_stats(sqlite3* h)
{
    DashboardStats stats;

    {
        Stmt stmt;
        stmt.ptr = db_prepare(h, "SELECT COUNT(*) FROM hosts;");
        if (stmt.ptr && sqlite3_step(stmt.ptr) == SQLITE_ROW)
            stats.hosts = sqlite3_column_int(stmt.ptr, 0);
    }

    {
        Stmt stmt;
        stmt.ptr = db_prepare(h, "SELECT COUNT(*) FROM ports WHERE state = 'open';");
        if (stmt.ptr && sqlite3_step(stmt.ptr) == SQLITE_ROW)
            stats.ports = sqlite3_column_int(stmt.ptr, 0);
    }

    {
        Stmt stmt;
        stmt.ptr = db_prepare(
            h,
            "SELECT COUNT(DISTINCT service) FROM ports "
            "WHERE state = 'open' AND service IS NOT NULL AND service != '';");
        if (stmt.ptr && sqlite3_step(stmt.ptr) == SQLITE_ROW)
            stats.services = sqlite3_column_int(stmt.ptr, 0);
    }

    {
        Stmt stmt;
        stmt.ptr = db_prepare(
            h,
            "SELECT created_at FROM scans WHERE deleted_at IS NULL ORDER BY id DESC LIMIT 1;");
        if (stmt.ptr && sqlite3_step(stmt.ptr) == SQLITE_ROW)
            stats.last_scan = col_text(stmt.ptr, 0);
    }

    return stats;
}

static std::vector<DashboardScanRow> read_recent_scans(sqlite3* h)
{
    return read_rows<DashboardScanRow>(
        h,
        "SELECT s.id, s.target, s.status, s.host_discovery_only, s.created_at, "
        "  (SELECT COUNT(*) FROM scan_hosts sh WHERE sh.scan_id = s.id) AS host_count, "
        "  (SELECT COUNT(*) FROM scan_ports sp WHERE sp.scan_id = s.id) AS port_count "
        "FROM scans s WHERE s.deleted_at IS NULL ORDER BY s.id DESC LIMIT 20;",
        scan_row_from_stmt);
}

static int select_topology_scan_id(const std::vector<DashboardScanRow>& scans)
{
    for (std::vector<DashboardScanRow>::const_iterator it = scans.begin(); it != scans.end(); ++it)
    {
        if (it->status == "completed" && !it->host_discovery_only)
            return it->id;
    }

    for (std::vector<DashboardScanRow>::const_iterator it = scans.begin(); it != scans.end(); ++it)
    {
        if (it->status == "completed")
            return it->id;
    }

    return 0;
}

static std::vector<DashboardHostRow> read_recent_hosts(sqlite3* h)
{
    return read_rows<DashboardHostRow>(h,
                                       "SELECT DISTINCT hosts.id, hosts.ip, hosts.name "
                                       "FROM hosts "
                                       "INNER JOIN ports ON ports.host_id = hosts.id "
                                       "WHERE ports.state = 'open' "
                                       "ORDER BY hosts.id DESC LIMIT 50;",
                                       host_row_from_stmt);
}

static std::vector<DashboardPortRow> read_recent_ports(sqlite3* h)
{
    return read_rows<DashboardPortRow>(h,
                                       "SELECT ports.id, hosts.ip, ports.port, ports.service "
                                       "FROM ports "
                                       "LEFT JOIN hosts ON hosts.id = ports.host_id "
                                       "WHERE ports.state = 'open' "
                                       "ORDER BY ports.id DESC LIMIT 100;",
                                       port_row_from_stmt);
}

static DashboardChangeCard read_change_card(sqlite3* h, ScanRepository& repo)
{
    DashboardChangeCard card;

    std::unique_ptr<PersistedScanSummary> current = repo.get_latest_scan(h);
    if (!current || current->state != PersistedScanState::Completed)
        return card;

    std::unique_ptr<PersistedScanSummary> baseline =
        repo.get_previous_comparable_completed_scan(h, current->id);
    if (!baseline)
        return card;

    card.has_baseline = true;
    HostDiffResult host_diff = repo.get_host_diff_between_scans(h, baseline->id, current->id);
    PortDiffResult port_diff = repo.get_port_diff_between_scans(h, baseline->id, current->id);

    card.added_hosts    = static_cast<int>(host_diff.new_hosts.size());
    card.removed_hosts  = static_cast<int>(host_diff.disappeared_hosts.size());
    card.added_ports    = static_cast<int>(port_diff.new_open_ports.size());
    card.removed_ports  = static_cast<int>(port_diff.disappeared_ports.size());

    return card;
}

DashboardData read_dashboard_data(sqlite3* h, ScanRepository& repo)
{
    DashboardData data;
    data.stats          = read_dashboard_stats(h);
    data.changes        = read_change_card(h, repo);
    data.scans          = read_recent_scans(h);
    data.hosts          = read_recent_hosts(h);
    data.ports          = read_recent_ports(h);
    data.topology_scan_id = select_topology_scan_id(data.scans);
    return data;
}
