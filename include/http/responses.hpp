#ifndef HTTP_RESPONSES_HPP
#define HTTP_RESPONSES_HPP

#include "platform.hpp"
#include "httplib/httplib.h"
#include <string>

struct JsonError
{
    int status;
    std::string type;
    std::string message;
};

void set_json_response(httplib::Response& res, int status, const std::string& body);

void set_json_error(httplib::Response& res, const JsonError& error);

#endif
