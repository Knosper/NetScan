#ifndef HTTP_API_HEALTH_ROUTE_HPP
#define HTTP_API_HEALTH_ROUTE_HPP

#include "platform.hpp"
#include "httplib/httplib.h"
#include "service/health_service.hpp"
#include "util/logger.hpp"

void register_health_route(httplib::Server& svr, HealthService& service, Logger& logger, bool ui_enabled);

#endif
