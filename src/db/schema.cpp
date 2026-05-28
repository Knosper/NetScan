#include "db/schema.hpp"
#include "util/logger.hpp"

#include <sqlite3.h>

namespace
{

void exec_sql(sqlite3* h, const std::string& sql)
{
    char* err = nullptr;

    if (sqlite3_exec(h, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK)
    {
        std::string error = err ? err : "unknown sqlite error";
        sqlite3_free(err);
        throw std::runtime_error(error);
    }
}

void create_secret_tables(sqlite3* h)
{
    exec_sql(h,
        "CREATE TABLE IF NOT EXISTS secrets ("
            "name TEXT PRIMARY KEY,"
            "hash TEXT NOT NULL,"
            "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ");"
    );
}

void create_scan_tables(sqlite3* h)
{
    exec_sql(h,
        "CREATE TABLE IF NOT EXISTS scans ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "target TEXT,"
            "status TEXT NOT NULL,"
            "host_discovery_only INTEGER NOT NULL DEFAULT 1,"
            "requested_ports TEXT NOT NULL DEFAULT '',"
            "port_coverage_known INTEGER NOT NULL DEFAULT 0,"
            "created_at TEXT DEFAULT CURRENT_TIMESTAMP,"
            "started_at TEXT NULL,"
            "finished_at TEXT NULL,"
            "message TEXT NOT NULL DEFAULT '',"
            "command TEXT NOT NULL DEFAULT '',"
            "exit_code INTEGER NULL,"
            "stderr_text TEXT NOT NULL DEFAULT '',"
            "deleted_at TEXT NULL"
        ");"
    );

    exec_sql(h,
        "CREATE TABLE IF NOT EXISTS scan_hosts ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "scan_id INTEGER NOT NULL,"
            "ip TEXT NOT NULL,"
            "name TEXT NOT NULL DEFAULT '',"
            "FOREIGN KEY(scan_id) REFERENCES scans(id)"
        ");"
    );

    exec_sql(h,
        "CREATE TABLE IF NOT EXISTS scan_ports ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "scan_id INTEGER NOT NULL,"
            "host_ip TEXT NOT NULL,"
            "port INTEGER NOT NULL,"
            "protocol TEXT NOT NULL DEFAULT 'tcp',"
            "state TEXT NOT NULL DEFAULT 'open',"
            "service TEXT NOT NULL DEFAULT '',"
            "FOREIGN KEY(scan_id) REFERENCES scans(id)"
        ");"
    );

    exec_sql(h,
        "CREATE TABLE IF NOT EXISTS scan_port_coverage ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "scan_id INTEGER NOT NULL,"
            "protocol TEXT NOT NULL DEFAULT 'tcp',"
            "start_port INTEGER NOT NULL,"
            "end_port INTEGER NOT NULL,"
            "FOREIGN KEY(scan_id) REFERENCES scans(id)"
        ");"
    );

    exec_sql(h,
        "CREATE TABLE IF NOT EXISTS scan_notes ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "scan_id INTEGER NOT NULL,"
            "body TEXT NOT NULL DEFAULT '',"
            "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "FOREIGN KEY(scan_id) REFERENCES scans(id)"
        ");"
    );

    exec_sql(h,
        "CREATE TABLE IF NOT EXISTS scan_diff_acknowledgements ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "category TEXT NOT NULL,"
            "host_ip TEXT NOT NULL,"
            "port INTEGER NOT NULL DEFAULT 0,"
            "acknowledged_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ");"
    );

    exec_sql(h,
        "CREATE TABLE IF NOT EXISTS scan_profiles ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "name TEXT NOT NULL,"
            "target TEXT NOT NULL,"
            "ports TEXT NOT NULL DEFAULT '',"
            "host_discovery_only INTEGER NOT NULL DEFAULT 0,"
            "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ");"
    );
}

void create_host_tables(sqlite3* h)
{
    exec_sql(h,
        "CREATE TABLE IF NOT EXISTS hosts ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "ip TEXT UNIQUE,"
            "name TEXT"
        ");"
    );

    exec_sql(h,
        "CREATE TABLE IF NOT EXISTS ports ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "host_id INTEGER,"
            "port INTEGER,"
            "protocol TEXT NOT NULL DEFAULT 'tcp',"
            "state TEXT NOT NULL DEFAULT 'open',"
            "service TEXT,"
            "last_open_scan_id INTEGER NOT NULL DEFAULT 0,"
            "last_observed_scan_id INTEGER NOT NULL DEFAULT 0"
        ");"
    );

}

void create_metadata_tables(sqlite3* h)
{
    exec_sql(h,
        "CREATE TABLE IF NOT EXISTS host_meta ("
            "ip TEXT PRIMARY KEY,"
            "display_name TEXT NOT NULL DEFAULT '',"
            "role TEXT NOT NULL DEFAULT '',"
            "tags TEXT NOT NULL DEFAULT ''"
        ");"
    );

    exec_sql(h,
        "CREATE TABLE IF NOT EXISTS app_metadata ("
            "key TEXT PRIMARY KEY,"
            "value TEXT NOT NULL DEFAULT ''"
        ");"
    );

    exec_sql(h,
        "CREATE TABLE IF NOT EXISTS scheduled_jobs ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "target TEXT NOT NULL,"
            "ports TEXT NOT NULL DEFAULT '',"
            "host_discovery_only INTEGER NOT NULL DEFAULT 0,"
            "interval_seconds INTEGER NOT NULL,"
            "enabled INTEGER NOT NULL DEFAULT 1,"
            "last_run_at TEXT,"
            "next_run_at TEXT"
        ");"
    );

    exec_sql(h,
        "CREATE TABLE IF NOT EXISTS user_allowed_targets ("
            "id INTEGER PRIMARY KEY,"
            "cidr TEXT UNIQUE NOT NULL,"
            "created_at INTEGER"
        ");"
    );
}

void create_presence_tables(sqlite3* h)
{
    exec_sql(h,
        "CREATE TABLE IF NOT EXISTS presence_trackers ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "target TEXT NOT NULL,"
            "check_type TEXT NOT NULL,"
            "port INTEGER NOT NULL DEFAULT 0,"
            "url TEXT NOT NULL DEFAULT '',"
            "interval_seconds INTEGER NOT NULL DEFAULT 300,"
            "timeout_ms INTEGER NOT NULL DEFAULT 3000,"
            "enabled INTEGER NOT NULL DEFAULT 1,"
            "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ");"
    );

    exec_sql(h,
        "CREATE TABLE IF NOT EXISTS presence_results ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "tracker_id INTEGER NOT NULL,"
            "status TEXT NOT NULL,"
            "latency_ms INTEGER NULL,"
            "error TEXT NOT NULL DEFAULT '',"
            "checked_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "FOREIGN KEY(tracker_id) REFERENCES presence_trackers(id) ON DELETE CASCADE"
        ");"
    );
}

void create_indexes(sqlite3* h)
{
    exec_sql(h, "CREATE UNIQUE INDEX IF NOT EXISTS idx_ports_unique ON ports(host_id, port, protocol);");
    exec_sql(h, "CREATE UNIQUE INDEX IF NOT EXISTS idx_scan_hosts_unique ON scan_hosts(scan_id, ip);");
    exec_sql(h, "CREATE UNIQUE INDEX IF NOT EXISTS idx_scan_ports_unique ON scan_ports(scan_id, host_ip, port, protocol);");
    exec_sql(h, "CREATE INDEX IF NOT EXISTS idx_scan_ports_host_history ON scan_ports(host_ip, scan_id, port);");
    exec_sql(h, "CREATE UNIQUE INDEX IF NOT EXISTS idx_scan_port_coverage_unique ON scan_port_coverage(scan_id, protocol, start_port, end_port);");
    exec_sql(h, "CREATE INDEX IF NOT EXISTS idx_scan_port_coverage_lookup ON scan_port_coverage(scan_id, protocol, start_port, end_port);");
    exec_sql(h, "CREATE INDEX IF NOT EXISTS idx_scan_notes_scan_id ON scan_notes(scan_id);");
    exec_sql(h, "CREATE UNIQUE INDEX IF NOT EXISTS idx_scan_diff_ack_unique ON scan_diff_acknowledgements(category, host_ip, port);");
    exec_sql(h, "CREATE INDEX IF NOT EXISTS idx_presence_trackers_enabled ON presence_trackers(enabled);");
    exec_sql(h, "CREATE INDEX IF NOT EXISTS idx_presence_results_tracker_time ON presence_results(tracker_id, checked_at DESC, id DESC);");
    exec_sql(h, "CREATE INDEX IF NOT EXISTS idx_presence_results_time ON presence_results(checked_at DESC, id DESC);");
    exec_sql(h, "CREATE UNIQUE INDEX IF NOT EXISTS idx_scan_profiles_name_unique ON scan_profiles(name);");
}

void create_all_tables(sqlite3* h)
{
    create_secret_tables(h);
    create_scan_tables(h);
    create_host_tables(h);
    create_metadata_tables(h);
    create_presence_tables(h);
    create_indexes(h);
}

} // namespace

void init_schema(Database& db, Logger& logger)
{
    db.write([&logger](sqlite3* h)
    {
        logger.info("Initializing database schema");
        create_all_tables(h);
        logger.info("Schema initialization completed");
    });
}
