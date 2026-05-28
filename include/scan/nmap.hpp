#ifndef NMAP_HPP
#define NMAP_HPP

#include "health/status.hpp"
#include <string>

struct NmapCheckResult
{
    HealthCheckStatus status;
    std::string       detail;
};

NmapCheckResult check_nmap();

#endif
