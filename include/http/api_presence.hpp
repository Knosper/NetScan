#ifndef HTTP_API_PRESENCE_HPP
#define HTTP_API_PRESENCE_HPP

#include "platform.hpp"
#include "httplib/httplib.h"
#include "service/presence_service.hpp"
#include "util/logger.hpp"

void register_presence_routes(httplib::Server& svr, PresenceService& service, Logger& logger);

#endif
