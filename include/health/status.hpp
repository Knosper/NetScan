#ifndef HEALTH_STATUS_HPP
#define HEALTH_STATUS_HPP

#include <string>

enum class AppStatus
{
    Ok,
    Degraded,
    Error
};

enum class HealthCheckStatus
{
    Ok,
    Missing,
    Error
};

inline std::string to_string(AppStatus status)
{
    switch (status)
    {
    case AppStatus::Ok:
        return "ok";
    case AppStatus::Degraded:
        return "degraded";
    case AppStatus::Error:
        return "error";
    }

    return "error";
}

inline std::string to_string(HealthCheckStatus status)
{
    switch (status)
    {
    case HealthCheckStatus::Ok:
        return "ok";
    case HealthCheckStatus::Missing:
        return "missing";
    case HealthCheckStatus::Error:
        return "error";
    }

    return "error";
}

#endif