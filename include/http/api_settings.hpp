#ifndef HTTP_API_SETTINGS_HPP
#define HTTP_API_SETTINGS_HPP

#include "platform.hpp"
#include "httplib/httplib.h"
#include "service/settings_service.hpp"
#include "util/logger.hpp"

void register_settings_routes(httplib::Server& svr, SettingsService& service, Logger& logger);

#endif
