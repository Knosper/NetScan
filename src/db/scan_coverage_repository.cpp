#include "db/scan_coverage_repository.hpp"
#include "db/sqlite_helpers.hpp"
#include <sqlite3.h>

namespace
{
bool update_scan_port_coverage_known(sqlite3* h, int scan_id, bool known)
{
    Stmt stmt;
    stmt.ptr = db_prepare(h, "UPDATE scans SET port_coverage_known=? WHERE id=?;");
    if (!stmt.ptr)
        return false;

    sqlite3_bind_int(stmt.ptr, 1, known ? 1 : 0);
    sqlite3_bind_int(stmt.ptr, 2, scan_id);
    return exec_write_step(stmt.ptr);
}

bool clear_scan_port_coverage(sqlite3* h, int scan_id)
{
    Stmt stmt;
    stmt.ptr = db_prepare(h, "DELETE FROM scan_port_coverage WHERE scan_id=?;");
    if (!stmt.ptr)
        return false;

    sqlite3_bind_int(stmt.ptr, 1, scan_id);
    return exec_write_step(stmt.ptr);
}

bool is_valid_port_coverage_range(const PortCoverageRange& range)
{
    return range.start_port > 0 && range.end_port > 0 && range.start_port <= range.end_port;
}

bool insert_tcp_port_coverage_ranges(sqlite3* h, int scan_id,
                                     const std::vector<PortCoverageRange>& ranges)
{
    Stmt stmt;
    stmt.ptr = db_prepare(
        h, "INSERT INTO scan_port_coverage (scan_id, protocol, start_port, end_port) "
           "VALUES (?, 'tcp', ?, ?);");
    if (!stmt.ptr)
        return false;

    for (const PortCoverageRange& range : ranges)
    {
        if (!is_valid_port_coverage_range(range))
            return false;

        sqlite3_bind_int(stmt.ptr, 1, scan_id);
        sqlite3_bind_int(stmt.ptr, 2, range.start_port);
        sqlite3_bind_int(stmt.ptr, 3, range.end_port);
        if (!finish_write_step(stmt.ptr))
            return false;
    }

    return true;
}
}

std::vector<ScanPortCoverageRow> ScanCoverageRepository::list_scan_port_coverage(sqlite3* h, int scan_id)
{
    const char* sql =
        "SELECT protocol, start_port, end_port "
        "FROM scan_port_coverage WHERE scan_id=? "
        "ORDER BY protocol ASC, start_port ASC, end_port ASC;";
    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    std::vector<ScanPortCoverageRow> rows;
    if (!stmt.ptr)
        return rows;

    sqlite3_bind_int(stmt.ptr, 1, scan_id);
    while (sqlite3_step(stmt.ptr) == SQLITE_ROW)
    {
        ScanPortCoverageRow row;
        row.protocol = col_text(stmt.ptr, 0);
        row.start_port = sqlite3_column_int(stmt.ptr, 1);
        row.end_port = sqlite3_column_int(stmt.ptr, 2);
        rows.push_back(row);
    }
    return rows;
}

bool ScanCoverageRepository::scan_has_known_port_coverage(sqlite3* h, int scan_id)
{
    Stmt stmt;
    stmt.ptr = db_prepare(h, "SELECT port_coverage_known FROM scans WHERE id=? LIMIT 1;");
    if (!stmt.ptr)
        return false;

    sqlite3_bind_int(stmt.ptr, 1, scan_id);
    if (sqlite3_step(stmt.ptr) != SQLITE_ROW)
        return false;

    return sqlite3_column_int(stmt.ptr, 0) != 0;
}

bool ScanCoverageRepository::scan_covers_port(sqlite3* h, int scan_id, int port,
                                       const std::string& protocol)
{
    if (scan_id <= 0 || port <= 0 || port > 65535 || protocol.empty())
        return false;

    Stmt stmt;
    stmt.ptr = db_prepare(
        h, "SELECT 1 FROM scan_port_coverage "
           "WHERE scan_id=? AND protocol=? AND start_port<=? AND end_port>=? LIMIT 1;");
    if (!stmt.ptr)
        return false;

    sqlite3_bind_int(stmt.ptr, 1, scan_id);
    sqlite3_bind_text(stmt.ptr, 2, protocol.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.ptr, 3, port);
    sqlite3_bind_int(stmt.ptr, 4, port);
    return sqlite3_step(stmt.ptr) == SQLITE_ROW;
}

bool ScanCoverageRepository::replace_scan_port_coverage(sqlite3* h, int scan_id,
                                                  const ScanPortCoverage& coverage)
{
    if (scan_id <= 0)
        return false;

    if (!update_scan_port_coverage_known(h, scan_id, coverage.known))
        return false;
    if (!clear_scan_port_coverage(h, scan_id))
        return false;

    if (coverage.tcp_ranges.empty())
        return true;

    return insert_tcp_port_coverage_ranges(h, scan_id, coverage.tcp_ranges);
}
