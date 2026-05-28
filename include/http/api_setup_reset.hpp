#ifndef API_SETUP_RESET_HPP
#define API_SETUP_RESET_HPP

#include "httplib/httplib.h"
#include <string>

#include "util/logger.hpp"

void register_setup_reset_route(httplib::Server& svr,
                                 const std::string& config_path,
                                 Logger& logger);

#endif
