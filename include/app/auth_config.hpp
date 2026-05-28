#ifndef APP_AUTH_CONFIG_HPP
#define APP_AUTH_CONFIG_HPP

#include <string>

struct AuthConfig
{
    std::string admin_api_key_hash;
    std::string restricted_api_key_hash;
};

bool is_open_mode(const std::string& host, const AuthConfig& auth_config);

#endif
