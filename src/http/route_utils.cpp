#include "http/route_utils.hpp"
#include "http/http_status.hpp"
#include "http/responses.hpp"
#include "scan/target_validation.hpp"
#include "util/string_utils.hpp"

#include <cctype>
#include <climits>
#include <cerrno>
#include <algorithm>
#include <cstdlib>

namespace
{
const std::size_t k_max_json_request_body_bytes = 64U * 1024U;

bool validate_port_value(int port_value, const std::string& error_prefix, std::string& error)
{
    if (port_value >= 1 && port_value <= 65535)
        return true;
    error = error_prefix + " must be between 1 and 65535";
    return false;
}
}

bool try_parse_path_id(const httplib::Request& req, std::size_t match_index, int& id)
{
    if (req.matches.size() <= match_index)
        return false;

    errno = 0;
    char* end = nullptr;
    long parsed = std::strtol(req.matches[match_index].str().c_str(), &end, 10);
    if (errno == ERANGE || end == nullptr || *end != '\0' || parsed <= 0 || parsed > INT_MAX)
        return false;

    id = static_cast<int>(parsed);
    return true;
}


bool try_parse_host_ip(const httplib::Request& req, std::string& host_ip)
{
    if (req.matches.size() < 2)
        return false;

    const std::string candidate = req.matches[1].str();
    if (candidate.empty())
        return false;

    if (candidate.find(':') != std::string::npos)
    {
        if (!is_valid_ipv6_literal(candidate))
            return false;
    }
    else if (!is_valid_ipv4_literal(candidate))
    {
        return false;
    }

    host_ip = candidate;
    return true;
}

bool has_json_content_type(const httplib::Request& req)
{
    if (!req.has_header("Content-Type"))
        return false;

    std::string content_type = req.get_header_value("Content-Type");
    const std::string::size_type semicolon_pos = content_type.find(';');
    if (semicolon_pos != std::string::npos)
        content_type = content_type.substr(0, semicolon_pos);

    content_type = util::trim(content_type);
    std::transform(content_type.begin(), content_type.end(), content_type.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return content_type == "application/json";
}

std::size_t max_json_request_body_bytes()
{
    return k_max_json_request_body_bytes;
}

bool is_request_body_too_large(const httplib::Request& req)
{
    return req.body.size() > k_max_json_request_body_bytes;
}

std::string json_body_too_large_message()
{
    return "request body exceeds " + std::to_string(k_max_json_request_body_bytes) + " bytes";
}

bool ensure_json_object(const nlohmann::json& body, std::string& error)
{
    if (body.is_discarded() || !body.is_object())
    {
        error = "request body must be a JSON object";
        return false;
    }
    return true;
}

bool parse_positive_int_param(const httplib::Request& req, const char* name, int default_value,
                              int& value_out)
{
    if (!req.has_param(name))
    {
        value_out = default_value;
        return true;
    }

    errno = 0;
    char* end = nullptr;
    const std::string value = req.get_param_value(name);
    long parsed = std::strtol(value.c_str(), &end, 10);
    if (errno == ERANGE || end == nullptr || *end != '\0' || parsed < 0 || parsed > INT_MAX)
        return false;

    value_out = static_cast<int>(parsed);
    return true;
}

bool extract_target_field(const nlohmann::json& body, std::string& target, std::string& error)
{
    nlohmann::json::const_iterator target_it = body.find("target");
    if (target_it == body.end() || !target_it->is_string())
    {
        error = "field 'target' must be a string";
        return false;
    }

    target = util::trim(target_it->get<std::string>());
    return true;
}

bool extract_requested_ports_field(const nlohmann::json& body, std::string& ports,
                                   std::string& error, bool allow_port_alias)
{
    nlohmann::json::const_iterator ports_it = body.find("ports");
    nlohmann::json::const_iterator port_it = body.find("port");

    if (ports_it != body.end())
    {
        if (allow_port_alias && port_it != body.end())
        {
            error = "only one of 'port' or 'ports' may be specified";
            return false;
        }

        if (!ports_it->is_string())
        {
            error = "field 'ports' must be a string";
            return false;
        }

        ports = ports_it->get<std::string>();
    }

    if (!allow_port_alias || port_it == body.end())
        return true;

    if (port_it->is_string())
    {
        error = "field 'port' must be an integer, string is not allowed";
        return false;
    }

    if (!port_it->is_number_integer())
    {
        error = "field 'port' must be an integer";
        return false;
    }

    const int port_value = port_it->get<int>();
    if (!validate_port_value(port_value, "port", error))
        return false;

    ports = std::to_string(port_value);
    return true;
}

bool extract_host_discovery_field(const nlohmann::json& body, bool& host_discovery_only,
                                  std::string& error)
{
    nlohmann::json::const_iterator host_only_it = body.find("host_discovery_only");
    if (host_only_it == body.end())
        return true;

    if (!host_only_it->is_boolean())
    {
        error = "field 'host_discovery_only' must be a boolean";
        return false;
    }

    host_discovery_only = host_only_it->get<bool>();
    return true;
}

bool read_optional_string_field(const nlohmann::json& body, const char* name, std::string& out,
                                std::string& error)
{
    nlohmann::json::const_iterator it = body.find(name);
    if (it == body.end())
        return true;
    if (!it->is_string())
    {
        error = std::string("field '") + name + "' must be a string";
        return false;
    }
    out = util::trim(it->get<std::string>());
    return true;
}

bool read_optional_int_field(const nlohmann::json& body, const char* name, int& out,
                             std::string& error)
{
    nlohmann::json::const_iterator it = body.find(name);
    if (it == body.end() || it->is_null())
        return true;
    if (!it->is_number_integer())
    {
        error = std::string("field '") + name + "' must be an integer";
        return false;
    }
    out = it->get<int>();
    return true;
}

bool read_optional_bool_field(const nlohmann::json& body, const char* name, bool& out,
                              std::string& error)
{
    nlohmann::json::const_iterator it = body.find(name);
    if (it == body.end())
        return true;
    if (!it->is_boolean())
    {
        error = std::string("field '") + name + "' must be a boolean";
        return false;
    }
    out = it->get<bool>();
    return true;
}

bool reject_if_body_too_large(const httplib::Request& req, httplib::Response& res)
{
    if (!is_request_body_too_large(req))
        return false;

    set_json_error(res, JsonError{http_status::PAYLOAD_TOO_LARGE, "payload_too_large",
                                  json_body_too_large_message()});
    return true;
}

bool validate_json_post_request(const httplib::Request& req, httplib::Response& res)
{
    if (!has_json_content_type(req))
    {
        set_json_error(res, JsonError{http_status::UNSUPPORTED_MEDIA_TYPE,
                                      "unsupported_media_type",
                                      "Content-Type must be application/json"});
        return false;
    }
    if (reject_if_body_too_large(req, res))
        return false;

    return true;
}
