#ifndef HTTP_API_PROFILES_HPP
#define HTTP_API_PROFILES_HPP

#include "platform.hpp"
#include "httplib/httplib.h"
#include "service/profile_service.hpp"

void register_profile_routes(httplib::Server& svr, ProfileService& service);

#endif
