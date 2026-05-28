#include "db/scan_host_repository.hpp"
#include "db/sqlite_helpers.hpp"
#include <sqlite3.h>


bool ScanHostRepository::insert_host(sqlite3* h, const std::string& ip, const std::string& name)
{
    if (ip.empty())
        return false;

    return insert_hosts(h, std::vector<HostRecordParams>{HostRecordParams{ip, name}});
}

bool ScanHostRepository::insert_hosts(sqlite3* h, const std::vector<HostRecordParams>& hosts)
{
    if (hosts.empty())
        return true;

    const char* sql = "INSERT INTO hosts (ip, name) VALUES (?, ?) "
                      "ON CONFLICT(ip) DO UPDATE SET name=excluded.name;";
    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (!stmt.ptr)
        return false;

    for (const auto& host : hosts)
    {
        if (host.ip.empty())
            return false;

        sqlite3_bind_text(stmt.ptr, 1, host.ip.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt.ptr, 2, host.name.c_str(), -1, SQLITE_TRANSIENT);
        if (!finish_write_step(stmt.ptr))
            return false;
    }

    return true;
}

bool ScanHostRepository::insert_scan_host(sqlite3* h, int scan_id, const std::string& ip,
                                          const std::string& name)
{
    if (scan_id <= 0 || ip.empty())
        return false;

    return insert_scan_hosts(h, scan_id, std::vector<HostRecordParams>{HostRecordParams{ip, name}});
}

bool ScanHostRepository::insert_scan_hosts(sqlite3* h, int scan_id,
                                           const std::vector<HostRecordParams>& hosts)
{
    if (hosts.empty())
        return true;

    if (scan_id <= 0)
        return false;

    const char* sql =
        "INSERT INTO scan_hosts (scan_id, ip, name) VALUES (?, ?, ?) "
        "ON CONFLICT(scan_id, ip) DO UPDATE SET name=excluded.name;";

    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (!stmt.ptr)
        return false;

    for (const auto& host : hosts)
    {
        if (host.ip.empty())
            return false;

        sqlite3_bind_int(stmt.ptr, 1, scan_id);
        sqlite3_bind_text(stmt.ptr, 2, host.ip.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt.ptr, 3, host.name.c_str(), -1, SQLITE_TRANSIENT);
        if (!finish_write_step(stmt.ptr))
            return false;
    }

    return true;
}

int ScanHostRepository::get_host_id_by_ip(sqlite3* h, const std::string& ip)
{
    const char* sql = "SELECT id FROM hosts WHERE ip=?;";
    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (!stmt.ptr)
        return -1;

    sqlite3_bind_text(stmt.ptr, 1, ip.c_str(), -1, SQLITE_TRANSIENT);

    int id = -1;
    if (sqlite3_step(stmt.ptr) == SQLITE_ROW)
        id = sqlite3_column_int(stmt.ptr, 0);
    return id;
}

std::vector<ScanHostRow> ScanHostRepository::list_scan_hosts(sqlite3* h, int scan_id)
{
    const char* sql =
        "SELECT ip, name FROM scan_hosts WHERE scan_id=? ORDER BY ip ASC;";
    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    std::vector<ScanHostRow> rows;
    if (!stmt.ptr)
        return rows;

    sqlite3_bind_int(stmt.ptr, 1, scan_id);
    while (sqlite3_step(stmt.ptr) == SQLITE_ROW)
    {
        ScanHostRow row;
        row.ip   = col_text(stmt.ptr, 0);
        row.name = col_text(stmt.ptr, 1);
        rows.push_back(row);
    }
    return rows;
}
