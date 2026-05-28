#ifndef HTTP_API_STATIC_HPP
#define HTTP_API_STATIC_HPP

#include "platform.hpp"
#include "httplib/httplib.h"
#include "app/app_context.hpp"

void register_static_routes(httplib::Server& svr, const AppContext& ctx);

#endif
