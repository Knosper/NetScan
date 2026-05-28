#ifndef APP_SETUP_SERVER_HPP
#define APP_SETUP_SERVER_HPP

#include <string>

struct SetupBind
{
    std::string host;
    int port = 8080;
};

SetupBind resolve_setup_bind(const std::string& absolute_config_path);

#endif
