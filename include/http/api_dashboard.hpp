#ifndef HTTP_API_DASHBOARD_HPP
#define HTTP_API_DASHBOARD_HPP

#include "platform.hpp"
#include "httplib/httplib.h"
#include "service/dashboard_service.hpp"
#include "util/logger.hpp"

void register_dashboard_routes(httplib::Server& svr, DashboardService& service, Logger& logger);

#endif
