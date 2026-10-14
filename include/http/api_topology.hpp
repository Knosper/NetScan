#ifndef HTTP_API_TOPOLOGY_HPP
#define HTTP_API_TOPOLOGY_HPP

#include "platform.hpp"
#include "httplib/httplib.h"
#include "service/topology_service.hpp"
#include "service/scan_service.hpp"
#include "service/settings_service.hpp"
#include "util/logger.hpp"

void register_topology_routes(httplib::Server& svr, TopologyService& service,
                              ScanService& scan_service, SettingsService& settings_service,
                              Logger& logger);

#endif
