#ifndef STATIC_FILES_HPP
#define STATIC_FILES_HPP

#include "platform.hpp"
#include <string>
#include "httplib/httplib.h"

bool read_file(const std::string& path, std::string& content);
void serve_static_file(httplib::Response& res, const std::string& path,
                       const std::string& content_type);
std::string get_mime_type(const std::string& path);

#endif