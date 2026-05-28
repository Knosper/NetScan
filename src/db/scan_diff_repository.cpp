#include "db/scan_diff_repository.hpp"
#include "db/sqlite_helpers.hpp"
#include <sqlite3.h>

namespace
{
PortDiffEntry row_to_port_diff_entry(sqlite3_stmt* stmt)
{
    PortDiffEntry entry;
    entry.ip = col_text(stmt, 0);
    entry.port = sqlite3_column_int(stmt, 1);
    entry.service = col_text(stmt, 2);
    entry.name = col_text(stmt, 3);
    return entry;
}

HostDiffEntry row_to_host_diff_entry(sqlite3_stmt* stmt)
{
    HostDiffEntry entry;
    entry.ip = col_text(stmt, 0);
    entry.name = col_text(stmt, 1);
    return entry;
}

std::string make_port_key(const std::string& ip, int port)
{
    return ip + "\n" + std::to_string(port);
}

std::vector<HostDiffEntry> load_scan_hosts(sqlite3* h, int scan_id)
{
    const char* sql = "SELECT ip, name "
                      "FROM scan_hosts "
                      "WHERE scan_id=? "
                      "ORDER BY ip ASC;";

    std::vector<HostDiffEntry> hosts;

    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (!stmt.ptr)
        return hosts;

    sqlite3_bind_int(stmt.ptr, 1, scan_id);

    while (sqlite3_step(stmt.ptr) == SQLITE_ROW)
        hosts.push_back(row_to_host_diff_entry(stmt.ptr));
    return hosts;
}

std::unordered_set<std::string> collect_host_ips(const std::vector<HostDiffEntry>& hosts)
{
    std::unordered_set<std::string> ips;
    for (const auto& host : hosts)
        ips.insert(host.ip);
    return ips;
}

std::vector<HostDiffEntry> collect_missing_hosts(
    const std::vector<HostDiffEntry>& hosts, const std::unordered_set<std::string>& known_ips)
{
    std::vector<HostDiffEntry> diff;
    for (const auto& host : hosts)
    {
        if (known_ips.find(host.ip) == known_ips.end())
            diff.push_back(host);
    }
    return diff;
}
} // namespace

HostDiffResult ScanDiffRepository::get_host_diff_between_scans(sqlite3* h, int baseline_scan_id,
                                                             int current_scan_id)
{
    HostDiffResult result;

    auto baseline_hosts = load_scan_hosts(h, baseline_scan_id);
    auto current_hosts = load_scan_hosts(h, current_scan_id);
    auto baseline_ips = collect_host_ips(baseline_hosts);
    auto current_ips = collect_host_ips(current_hosts);

    result.new_hosts = collect_missing_hosts(current_hosts, baseline_ips);
    result.disappeared_hosts = collect_missing_hosts(baseline_hosts, current_ips);

    return result;
}

static std::unordered_set<std::string> load_port_keys(sqlite3* h, int scan_id)
{
    const char* sql = "SELECT host_ip, port "
                      "FROM scan_ports "
                      "WHERE scan_id=?;";

    std::unordered_set<std::string> keys;

    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (!stmt.ptr)
        return keys;

    sqlite3_bind_int(stmt.ptr, 1, scan_id);

    while (sqlite3_step(stmt.ptr) == SQLITE_ROW)
    {
        std::string ip = col_text(stmt.ptr, 0);
        int port = sqlite3_column_int(stmt.ptr, 1);
        keys.insert(make_port_key(ip, port));
    }
    return keys;
}

static std::vector<PortDiffEntry> load_new_ports(sqlite3* h, int current_scan_id,
                                                  const std::unordered_set<std::string>& baseline)
{
    const char* sql = "SELECT sp.host_ip, sp.port, sp.service, sh.name "
                      "FROM scan_ports sp "
                      "LEFT JOIN scan_hosts sh ON sh.scan_id = sp.scan_id AND sh.ip = sp.host_ip "
                      "WHERE sp.scan_id=?;";

    std::vector<PortDiffEntry> result;

    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (!stmt.ptr)
        return result;

    sqlite3_bind_int(stmt.ptr, 1, current_scan_id);

    while (sqlite3_step(stmt.ptr) == SQLITE_ROW)
    {
        PortDiffEntry entry = row_to_port_diff_entry(stmt.ptr);
        if (baseline.find(make_port_key(entry.ip, entry.port)) == baseline.end())
            result.push_back(entry);
    }
    return result;
}

PortDiffResult ScanDiffRepository::get_port_diff_between_scans(sqlite3* h, int baseline_scan_id,
                                                             int current_scan_id)
{
    PortDiffResult result;

    auto baseline_keys = load_port_keys(h, baseline_scan_id);
    auto current_keys  = load_port_keys(h, current_scan_id);

    result.new_open_ports     = load_new_ports(h, current_scan_id, baseline_keys);
    result.disappeared_ports  = load_new_ports(h, baseline_scan_id, current_keys);

    return result;
}

std::unordered_set<std::string> ScanDiffRepository::list_acknowledged_diff_keys(sqlite3* h)
{
    const char* sql =
        "SELECT category, host_ip, port "
        "FROM scan_diff_acknowledgements;";

    std::unordered_set<std::string> keys;
    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (!stmt.ptr)
        return keys;

    while (sqlite3_step(stmt.ptr) == SQLITE_ROW)
    {
        ScanDiffAcknowledgementKey key;
        key.category = col_text(stmt.ptr, 0);
        key.ip = col_text(stmt.ptr, 1);
        key.port = sqlite3_column_int(stmt.ptr, 2);
        key.has_port = key.port > 0;
        keys.insert(scan_diff_acknowledgement_key_id(key));
    }
    return keys;
}

bool ScanDiffRepository::set_diff_acknowledgement(sqlite3* h,
                                                const ScanDiffAcknowledgementKey& key)
{
    const char* sql =
        "INSERT INTO scan_diff_acknowledgements (category, host_ip, port) "
        "VALUES (?, ?, ?) "
        "ON CONFLICT(category, host_ip, port) DO UPDATE "
        "SET acknowledged_at=CURRENT_TIMESTAMP;";

    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (!stmt.ptr)
        return false;

    sqlite3_bind_text(stmt.ptr, 1, key.category.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.ptr, 2, key.ip.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.ptr, 3, key.port);

    return exec_write_step(stmt.ptr);
}

bool ScanDiffRepository::clear_diff_acknowledgement(sqlite3* h,
                                                  const ScanDiffAcknowledgementKey& key)
{
    const char* sql =
        "DELETE FROM scan_diff_acknowledgements "
        "WHERE category=? AND host_ip=? AND port=?;";

    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (!stmt.ptr)
        return false;

    sqlite3_bind_text(stmt.ptr, 1, key.category.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.ptr, 2, key.ip.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.ptr, 3, key.port);

    return exec_write_step(stmt.ptr);
}
