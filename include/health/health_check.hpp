#ifndef HEALTH_HEALTH_CHECK_HPP
#define HEALTH_HEALTH_CHECK_HPP

#include <sqlite3.h>
#include "health/health.hpp"
#include "scan/nmap.hpp"
#include "util/logger.hpp"

// REQUIRES: caller passes the sqlite3* handle from the enclosing
// Database::read()/write() access scope.
HealthResult get_health_result(sqlite3* h, Logger& logger, const NmapCheckResult& nmap_result,
                               const std::string& local_ip);

#endif
