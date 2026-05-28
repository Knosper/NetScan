#ifndef HTTP_API_HOST_HPP
#define HTTP_API_HOST_HPP

#include "platform.hpp"
#include "httplib/httplib.h"
#include "service/host_service.hpp"
#include "service/settings_service.hpp"
#include "util/logger.hpp"

void register_host_routes(httplib::Server& svr, HostService& service,
                          SettingsService& settings_service, Logger& logger);

#endif
