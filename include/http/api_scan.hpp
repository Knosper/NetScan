#ifndef HTTP_API_SCAN_HPP
#define HTTP_API_SCAN_HPP

#include "platform.hpp"
#include "httplib/httplib.h"
#include "service/scan_diff_service.hpp"
#include "service/scan_history_service.hpp"
#include "service/scan_service.hpp"
#include "service/settings_service.hpp"
#include "util/logger.hpp"

struct ScanRouteServices
{
    ScanService&        scan_service;
    ScanDiffService&    diff_service;
    ScanHistoryService& history_service;
    SettingsService&    settings_service;
    Logger&             logger;
};

void register_scan_routes(httplib::Server& svr, const ScanRouteServices& services);

#endif
