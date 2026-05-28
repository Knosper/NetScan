#include "health/health_check.hpp"
#include "db/sqlite_helpers.hpp"

namespace
{
std::string append_detail(const std::string& message, const std::string& detail)
{
    if (detail.empty())
        return message;
    return message + ": " + detail;
}
}

static void resolve_app_status(HealthResult& result)
{
    if (result.database_status == HealthCheckStatus::Error)
    {
        result.app_status = AppStatus::Error;
        result.message = "database unavailable";
    }
    else if (result.nmap_status == HealthCheckStatus::Error)
    {
        result.app_status = AppStatus::Degraded;
        result.message = "nmap check failed";
    }
    else if (result.nmap_status == HealthCheckStatus::Missing)
    {
        result.app_status = AppStatus::Degraded;
        result.message = "nmap not found in PATH";
    }
    else
    {
        result.app_status = AppStatus::Ok;
        result.message = "ok";
    }
}

static HealthCheckStatus check_database_health(sqlite3* h, Logger& logger)
{
    Stmt stmt;
    stmt.ptr = db_prepare(h, "SELECT 1");
    if (stmt.ptr == nullptr)
    {
        logger.error(std::string("Health check: sqlite3_prepare_v2 failed: ") +
                     db_error_message(h));
        return HealthCheckStatus::Error;
    }

    const int rc = sqlite3_step(stmt.ptr);
    if (rc != SQLITE_ROW)
    {
        logger.error(std::string("Health check: sqlite3_step failed (code ") +
                     std::to_string(rc) + "): " + db_error_message(h));
        return HealthCheckStatus::Error;
    }

    return HealthCheckStatus::Ok;
}

static HealthCheckStatus check_nmap_health(const NmapCheckResult& nmap_result, Logger& logger)
{
    if (nmap_result.status == HealthCheckStatus::Missing)
        logger.warn("Health check: nmap not found in PATH");
    else if (nmap_result.status == HealthCheckStatus::Error)
        logger.error(append_detail("Health check: nmap availability check failed",
                                   nmap_result.detail));
    return nmap_result.status;
}

HealthResult get_health_result(sqlite3* h, Logger& logger, const NmapCheckResult& nmap_result,
                               const std::string& local_ip)
{
    // REQUIRES: caller passes the sqlite3* handle from the enclosing DB scope.
    HealthResult result;

    result.database_status = check_database_health(h, logger);
    result.nmap_status = check_nmap_health(nmap_result, logger);
    result.message = "";
    result.local_ip = local_ip;

    resolve_app_status(result);

    return result;
}
