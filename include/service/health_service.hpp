#ifndef SERVICE_HEALTH_SERVICE_HPP
#define SERVICE_HEALTH_SERVICE_HPP

#include "db/database.hpp"
#include "health/health.hpp"
#include "util/logger.hpp"

class HealthService
{
public:
    HealthService(Database& db, Logger& logger, std::string local_ip);
    HealthResult get_health() const;

private:
    Database& db_;
    Logger& logger_;
    std::string local_ip_;
};

#endif
