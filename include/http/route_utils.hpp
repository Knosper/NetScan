#ifndef HTTP_ROUTE_UTILS_HPP
#define HTTP_ROUTE_UTILS_HPP

#include "platform.hpp"
#include "httplib/httplib.h"
#include <nlohmann/json.hpp>
#include <cstddef>
#include <string>

bool try_parse_path_id(const httplib::Request& req, std::size_t match_index, int& id);
bool try_parse_host_ip(const httplib::Request& req, std::string& host_ip);
bool has_json_content_type(const httplib::Request& req);
std::size_t max_json_request_body_bytes();
bool is_request_body_too_large(const httplib::Request& req);
std::string json_body_too_large_message();
bool ensure_json_object(const nlohmann::json& body, std::string& error);
bool parse_positive_int_param(const httplib::Request& req, const char* name, int default_value,
                              int& value_out);
bool extract_target_field(const nlohmann::json& body, std::string& target, std::string& error);
bool extract_requested_ports_field(const nlohmann::json& body, std::string& ports,
                                   std::string& error, bool allow_port_alias = true);
bool extract_host_discovery_field(const nlohmann::json& body, bool& host_discovery_only,
                                  std::string& error);

// Optional typed field extraction — leaves `out` unchanged when the field is absent.
// Returns false (and sets `error`) only when the field is present but has the wrong type.
// read_optional_string_field: also accepts null as absent; trims whitespace on success.
// read_optional_int_field: treats null as absent.
bool read_optional_string_field(const nlohmann::json& body, const char* name, std::string& out,
                                std::string& error);
bool read_optional_int_field(const nlohmann::json& body, const char* name, int& out,
                             std::string& error);
bool read_optional_bool_field(const nlohmann::json& body, const char* name, bool& out,
                              std::string& error);

// Returns true and sets the response error if the request body exceeds the size limit.
// Returns false if the body is within the allowed size.
bool reject_if_body_too_large(const httplib::Request& req, httplib::Response& res);

// Validates Content-Type and body size for JSON POST requests.
// Returns false and sets the response error on the first validation failure.
bool validate_json_post_request(const httplib::Request& req, httplib::Response& res);

#endif
