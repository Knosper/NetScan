#include "app/auth_config.hpp"

#include "app/config.hpp"

bool is_open_mode(const std::string& host, const AuthConfig& auth_config)
{
    return is_loopback_host(host) && auth_config.admin_api_key_hash.empty() &&
           auth_config.restricted_api_key_hash.empty();
}
