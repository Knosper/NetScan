#include "db/host_repository.hpp"
#include "db/sqlite_helpers.hpp"
#include <sqlite3.h>

namespace
{
HostOverview read_host_overview_row(sqlite3_stmt* stmt)
{
    HostOverview host;
    host.id = sqlite3_column_int(stmt, 0);
    host.ip = col_text(stmt, 1);
    host.name = col_text(stmt, 2);
    host.scan_count = sqlite3_column_int(stmt, 3);
    host.last_seen_at = col_text(stmt, 4);
    host.last_scan_id = sqlite3_column_int(stmt, 5);
    host.open_port_count = sqlite3_column_int(stmt, 6);
    host.meta.display_name = col_text(stmt, 7);
    host.meta.role = col_text(stmt, 8);
    host.meta.tags = col_text(stmt, 9);
    return host;
}

HostSummary read_host_summary_row(sqlite3_stmt* stmt)
{
    HostSummary host;
    host.id = sqlite3_column_int(stmt, 0);
    host.ip = col_text(stmt, 1);
    host.name = col_text(stmt, 2);
    return host;
}

HostHistoryScan read_host_history_scan_row(sqlite3_stmt* stmt)
{
    HostHistoryScan scan;
    scan.id = sqlite3_column_int(stmt, 0);
    scan.state = persisted_state_from_db(col_text(stmt, 1));
    scan.created_at = col_text(stmt, 2);
    scan.started_at = col_text(stmt, 3);
    scan.finished_at = col_text(stmt, 4);
    scan.deleted_at = col_text(stmt, 5);
    scan.deleted = !scan.deleted_at.empty();
    return scan;
}

HostHistoryPortRow read_host_history_port_row(sqlite3_stmt* stmt)
{
    HostHistoryPortRow row;
    row.scan_id = sqlite3_column_int(stmt, 0);
    row.port = sqlite3_column_int(stmt, 1);
    row.service = col_text(stmt, 2);
    row.state = col_text(stmt, 3);
    return row;
}


std::string build_host_filter_where(const HostListFilter& filter,
                                     std::vector<std::string>& bind_patterns)
{
    const bool has_search = !filter.search.empty();
    if (!has_search && !filter.open_ports_only)
        return "";

    std::string where = "WHERE ";
    if (has_search)
    {
        const std::string pattern = "%" + escape_like(filter.search) + "%";
        bind_patterns.push_back(pattern);
        bind_patterns.push_back(pattern);
        bind_patterns.push_back(pattern);
        bind_patterns.push_back(pattern);
        where += "(base.ip LIKE ? ESCAPE '\\' OR base.name LIKE ? ESCAPE '\\' "
                 "OR h.name LIKE ? ESCAPE '\\' OR hm.tags LIKE ? ESCAPE '\\') ";
    }
    if (has_search && filter.open_ports_only)
        where += "AND ";
    if (filter.open_ports_only)
        where += "COALESCE(port_agg.open_port_count, 0) > 0 ";
    return where;
}

const char* host_overview_base_sql()
{
    return
        "WITH base AS ("
        "    SELECT ip, MAX(name) AS name "
        "    FROM ("
        "        SELECT ip, name FROM hosts "
        "        UNION ALL "
        "        SELECT ip, name FROM scan_hosts"
        "    ) "
        "    GROUP BY ip"
        "), "
        "scan_agg AS ("
        "    SELECT sh.ip AS ip, "
        "           COUNT(DISTINCT sh.scan_id) AS scan_count, "
        "           MAX(sh.scan_id) AS last_scan_id "
        "    FROM scan_hosts sh "
        "    JOIN scans s ON s.id = sh.scan_id "
        "    WHERE s.deleted_at IS NULL "
        "    GROUP BY sh.ip"
        "), "
        "port_agg AS ("
        "    SELECT p.host_id AS host_id, COUNT(*) AS open_port_count "
        "    FROM ports p "
        "    WHERE p.state='open' "
        "    GROUP BY p.host_id"
        ") "
        "SELECT COALESCE(h.id, 0), base.ip, "
        "       COALESCE(NULLIF(h.name, ''), base.name), "
        "       COALESCE(scan_agg.scan_count, 0), "
        "       COALESCE(COALESCE(NULLIF(last_scan.finished_at, ''), last_scan.created_at), ''), "
        "       COALESCE(scan_agg.last_scan_id, 0), "
        "       COALESCE(port_agg.open_port_count, 0), "
        "       COALESCE(hm.display_name, ''), "
        "       COALESCE(hm.role, ''), "
        "       COALESCE(hm.tags, '') "
        "FROM base "
        "LEFT JOIN hosts h ON h.ip = base.ip "
        "LEFT JOIN host_meta hm ON hm.ip = base.ip "
        "LEFT JOIN scan_agg ON scan_agg.ip = base.ip "
        "LEFT JOIN scans last_scan ON last_scan.id = scan_agg.last_scan_id "
        "LEFT JOIN port_agg ON port_agg.host_id = h.id ";
}
} // namespace

HostRepository::HostRepository(Database& /*db*/) {}

std::vector<HostOverview> HostRepository::list_hosts_overview(sqlite3* h,
                                                               const HostListFilter& filter,
                                                               int limit, int offset)
{
    std::vector<std::string> bind_patterns;
    std::string full_sql = std::string(host_overview_base_sql())
                         + build_host_filter_where(filter, bind_patterns)
                         + "ORDER BY LOWER(COALESCE(NULLIF(h.name, ''), base.name, base.ip)), base.ip "
                           "LIMIT ? OFFSET ?;";

    Stmt stmt;
    stmt.ptr = db_prepare(h, full_sql.c_str());
    std::vector<HostOverview> rows;
    if (!stmt.ptr)
        return rows;

    int bind_index = 1;
    for (const auto& p : bind_patterns)
        sqlite3_bind_text(stmt.ptr, bind_index++, p.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.ptr, bind_index++, limit);
    sqlite3_bind_int(stmt.ptr, bind_index,   offset);

    while (sqlite3_step(stmt.ptr) == SQLITE_ROW)
        rows.push_back(read_host_overview_row(stmt.ptr));
    return rows;
}

int HostRepository::count_hosts_overview(sqlite3* h, const HostListFilter& filter)
{
    std::vector<std::string> bind_patterns;
    const std::string where = build_host_filter_where(filter, bind_patterns);
    const std::string full_sql = "SELECT COUNT(*) FROM (" +
                                  std::string(host_overview_base_sql()) + where + ") sub;";

    Stmt stmt;
    stmt.ptr = db_prepare(h, full_sql.c_str());
    if (!stmt.ptr)
        return 0;

    int bind_index = 1;
    for (const auto& p : bind_patterns)
        sqlite3_bind_text(stmt.ptr, bind_index++, p.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt.ptr) == SQLITE_ROW)
        return sqlite3_column_int(stmt.ptr, 0);
    return 0;
}

std::unique_ptr<HostSummary> HostRepository::get_host_by_ip(sqlite3* h, const std::string& ip)
{
    const char* sql = "SELECT COALESCE(h.id, 0), base.ip, COALESCE(NULLIF(h.name, ''), base.name) "
                      "FROM ("
                      "    SELECT ip, MAX(name) AS name "
                      "    FROM ("
                      "        SELECT ip, name FROM hosts "
                      "        UNION ALL "
                      "        SELECT ip, name FROM scan_hosts"
                      "    ) "
                      "    WHERE ip=? "
                      "    GROUP BY ip"
                      ") base "
                      "LEFT JOIN hosts h ON h.ip = base.ip;";

    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (!stmt.ptr)
        return nullptr;

    sqlite3_bind_text(stmt.ptr, 1, ip.c_str(), -1, SQLITE_TRANSIENT);

    std::unique_ptr<HostSummary> result;
    if (sqlite3_step(stmt.ptr) == SQLITE_ROW)
        result.reset(new HostSummary(read_host_summary_row(stmt.ptr)));
    return result;
}

std::vector<HostHistoryScan> HostRepository::list_host_history(sqlite3* h,
                                                                const std::string& ip)
{
    const char* sql =
        "SELECT DISTINCT s.id, s.status, s.created_at, s.started_at, s.finished_at, s.deleted_at "
        "FROM scans s "
        "LEFT JOIN scan_hosts sh ON s.id = sh.scan_id "
        "LEFT JOIN scan_ports sp ON s.id = sp.scan_id "
        "WHERE sh.ip=? OR sp.host_ip=? "
        "ORDER BY s.id DESC;";

    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    std::vector<HostHistoryScan> rows;
    if (!stmt.ptr)
        return rows;

    sqlite3_bind_text(stmt.ptr, 1, ip.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.ptr, 2, ip.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt.ptr) == SQLITE_ROW)
        rows.push_back(read_host_history_scan_row(stmt.ptr));
    return rows;
}

std::vector<HostHistoryPortRow> HostRepository::list_ports_for_host_history(
    sqlite3* h, const std::string& ip)
{
    const char* sql = "SELECT scan_id, port, service, state "
                      "FROM scan_ports "
                      "WHERE host_ip=? "
                      "ORDER BY scan_id DESC, port ASC;";

    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    std::vector<HostHistoryPortRow> rows;
    if (!stmt.ptr)
        return rows;

    sqlite3_bind_text(stmt.ptr, 1, ip.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt.ptr) == SQLITE_ROW)
        rows.push_back(read_host_history_port_row(stmt.ptr));
    return rows;
}
