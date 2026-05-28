#include "service/health_service.hpp"
#include "health/health_check.hpp"
#include "scan/nmap.hpp"
#include "util/logger.hpp"

HealthService::HealthService(Database& db, Logger& logger, std::string local_ip)
    : db_(db), logger_(logger), local_ip_(std::move(local_ip)) {}

HealthResult HealthService::get_health() const
{
    // nmap check has no DB dependency – run it before entering the DB read scope
    NmapCheckResult nmap_result = check_nmap();

    return db_.read([this, &nmap_result](sqlite3* h)
        { return get_health_result(h, logger_, nmap_result, local_ip_); });
}
