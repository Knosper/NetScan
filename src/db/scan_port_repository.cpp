#include "db/scan_port_repository.hpp"
#include "db/sqlite_helpers.hpp"
#include <sqlite3.h>

namespace
{
PersistedPortStateRow row_to_persisted_port_state(sqlite3_stmt* stmt)
{
    PersistedPortStateRow row;
    row.host_id = sqlite3_column_int(stmt, 0);
    row.port = sqlite3_column_int(stmt, 1);
    row.protocol = col_text(stmt, 2);
    row.state = col_text(stmt, 3);
    row.service = col_text(stmt, 4);
    row.last_open_scan_id = sqlite3_column_int(stmt, 5);
    row.last_observed_scan_id = sqlite3_column_int(stmt, 6);
    return row;
}

} // namespace

std::vector<ScanPortRow> ScanPortRepository::list_scan_ports(sqlite3* h, int scan_id)
{
    const char* sql =
        "SELECT host_ip, port, protocol, state, service "
        "FROM scan_ports WHERE scan_id=? ORDER BY host_ip ASC, port ASC;";
    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    std::vector<ScanPortRow> rows;
    if (!stmt.ptr)
        return rows;

    sqlite3_bind_int(stmt.ptr, 1, scan_id);
    while (sqlite3_step(stmt.ptr) == SQLITE_ROW)
    {
        ScanPortRow row;
        row.host_ip  = col_text(stmt.ptr, 0);
        row.port     = sqlite3_column_int(stmt.ptr, 1);
        row.protocol = col_text(stmt.ptr, 2);
        row.state    = col_text(stmt.ptr, 3);
        row.service  = col_text(stmt.ptr, 4);
        rows.push_back(row);
    }
    return rows;
}

std::vector<ScanPortRow> ScanPortRepository::list_topology_open_ports(sqlite3* h, int scan_id)
{
    // Returns open ports from this scan that are still provably open in the current port state.
    //
    // Conservative fallback: ports with no current state record, or no coverage tracking
    // (last_open_scan_id = 0), are included — unknown coverage must not be read as closed.
    //
    // A port is excluded only when a later covering scan observed it as not-open:
    //   last_observed_scan_id > last_open_scan_id  ->  evidence of closure.
    const char* sql =
        "SELECT sp.host_ip, sp.port, sp.protocol, sp.state, sp.service "
        "FROM scan_ports sp "
        "LEFT JOIN hosts h ON h.ip = sp.host_ip "
        "LEFT JOIN ports p ON p.host_id = h.id "
        "                  AND p.port = sp.port "
        "                  AND p.protocol = sp.protocol "
        "WHERE sp.scan_id = ? "
        "  AND sp.state = 'open' "
        "  AND ("
        "    p.id IS NULL "
        "    OR p.last_open_scan_id = 0 "
        "    OR p.last_observed_scan_id <= p.last_open_scan_id "
        "  ) "
        "ORDER BY sp.host_ip ASC, sp.port ASC;";
    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    std::vector<ScanPortRow> rows;
    if (!stmt.ptr)
        return rows;

    sqlite3_bind_int(stmt.ptr, 1, scan_id);
    while (sqlite3_step(stmt.ptr) == SQLITE_ROW)
    {
        ScanPortRow row;
        row.host_ip  = col_text(stmt.ptr, 0);
        row.port     = sqlite3_column_int(stmt.ptr, 1);
        row.protocol = col_text(stmt.ptr, 2);
        row.state    = col_text(stmt.ptr, 3);
        row.service  = col_text(stmt.ptr, 4);
        rows.push_back(row);
    }
    return rows;
}

std::vector<PersistedPortStateRow> ScanPortRepository::list_current_ports_for_host(sqlite3* h,
                                                                                  int host_id)
{
    const char* sql =
        "SELECT host_id, port, protocol, state, service, last_open_scan_id, last_observed_scan_id "
        "FROM ports WHERE host_id=? ORDER BY protocol ASC, port ASC;";
    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    std::vector<PersistedPortStateRow> rows;
    if (!stmt.ptr)
        return rows;

    sqlite3_bind_int(stmt.ptr, 1, host_id);
    while (sqlite3_step(stmt.ptr) == SQLITE_ROW)
        rows.push_back(row_to_persisted_port_state(stmt.ptr));
    return rows;
}

int ScanPortRepository::prune_closed_ports(sqlite3* h)
{
    const char* sql = "DELETE FROM ports WHERE state = 'closed';";
    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (!stmt.ptr)
        return -1;

    const bool ok = exec_write_step(stmt.ptr);
    return ok ? db_changes(h) : -1;
}

bool ScanPortRepository::delete_ports_for_host(sqlite3* h, int host_id)
{
    const char* sql = "DELETE FROM ports WHERE host_id=?;";
    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (!stmt.ptr)
        return false;

    sqlite3_bind_int(stmt.ptr, 1, host_id);
    return exec_write_step(stmt.ptr);
}

bool ScanPortRepository::insert_port(sqlite3* h, const PortRecordParams& params)
{
    return insert_ports(h, std::vector<PortRecordParams>{params});
}

bool ScanPortRepository::insert_ports(sqlite3* h, const std::vector<PortRecordParams>& ports)
{
    if (ports.empty())
        return true;

    const char* sql =
        "INSERT INTO ports (host_id, port, protocol, state, service, "
        "last_open_scan_id, last_observed_scan_id) "
        "VALUES (?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(host_id, port, protocol) DO UPDATE "
        "SET state=excluded.state, service=excluded.service, "
        "last_open_scan_id=excluded.last_open_scan_id, "
        "last_observed_scan_id=excluded.last_observed_scan_id;";

    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (!stmt.ptr)
        return false;

    for (const auto& port : ports)
    {
        sqlite3_bind_int(stmt.ptr, 1, port.host_id);
        sqlite3_bind_int(stmt.ptr, 2, port.port);
        sqlite3_bind_text(stmt.ptr, 3, port.protocol.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt.ptr, 4, port.state.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt.ptr, 5, port.service.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt.ptr, 6, port.last_open_scan_id);
        sqlite3_bind_int(stmt.ptr, 7, port.last_observed_scan_id);
        if (!finish_write_step(stmt.ptr))
            return false;
    }

    return true;
}

bool ScanPortRepository::insert_scan_port(sqlite3* h, const ScanPortParams& params)
{
    if (params.scan_id <= 0 || params.host_ip.empty())
        return false;

    return insert_scan_ports(h, std::vector<ScanPortParams>{params});
}

bool ScanPortRepository::insert_scan_ports(sqlite3* h, const std::vector<ScanPortParams>& ports)
{
    if (ports.empty())
        return true;

    const char* sql =
        "INSERT INTO scan_ports (scan_id, host_ip, port, protocol, state, service) "
        "VALUES (?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(scan_id, host_ip, port, protocol) DO UPDATE "
        "SET state=excluded.state, service=excluded.service;";

    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (!stmt.ptr)
        return false;

    for (const auto& port : ports)
    {
        if (port.scan_id <= 0 || port.host_ip.empty())
            return false;

        sqlite3_bind_int(stmt.ptr, 1, port.scan_id);
        sqlite3_bind_text(stmt.ptr, 2, port.host_ip.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt.ptr, 3, port.port);
        sqlite3_bind_text(stmt.ptr, 4, port.protocol.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt.ptr, 5, port.state.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt.ptr, 6, port.service.c_str(), -1, SQLITE_TRANSIENT);
        if (!finish_write_step(stmt.ptr))
            return false;
    }

    return true;
}

