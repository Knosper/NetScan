#ifndef HTTP_API_GUARD_HPP
#define HTTP_API_GUARD_HPP

#include "app/auth_config.hpp"
#include "app/config.hpp"
#include "platform.hpp"
#include "httplib/httplib.h"
#include <regex>
#include <string>
#include <utility>
#include <vector>

// Registry of registered API routes used to distinguish 404 (unknown path)
// from 405 (path known but method not registered).
struct MethodRegistry
{
    // Entry: compiled pattern + HTTP method string.
    struct Entry
    {
        std::regex  pattern;
        std::string method;
    };

    std::vector<Entry> entries;

    // Add a route. `pattern` must be the same regex string passed to httplib.
    void add(const std::string& pattern, const std::string& method);

    // Returns comma-separated allowed methods when `path` matches at least one
    // registered pattern, regardless of method. Returns empty string otherwise.
    std::string find_allowed(const std::string& path) const;
};

void register_api_key_guard(httplib::Server& svr, const AppConfig& config,
                            const AuthConfig& auth_config);
void register_api_response_headers(httplib::Server& svr, const AppConfig& config);
void register_error_handlers(httplib::Server& svr, const MethodRegistry& registry);

#endif
