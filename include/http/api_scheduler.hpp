#ifndef HTTP_API_SCHEDULER_HPP
#define HTTP_API_SCHEDULER_HPP

#include "platform.hpp"
#include "httplib/httplib.h"
#include "service/scheduler_service.hpp"

void register_scheduler_routes(httplib::Server& svr, SchedulerService& service);

#endif
