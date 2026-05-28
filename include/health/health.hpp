#ifndef HEALTH_HEALTH_HPP
#define HEALTH_HEALTH_HPP

#include <string>
#include "status.hpp"

struct HealthResult
{
    AppStatus app_status;
    HealthCheckStatus database_status;
    HealthCheckStatus nmap_status;
    std::string message;
    std::string local_ip;
};

#endif