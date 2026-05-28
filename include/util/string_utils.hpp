#ifndef UTIL_STRING_UTILS_HPP
#define UTIL_STRING_UTILS_HPP

#include <string>

namespace util
{

std::string trim(const std::string& s);
bool is_numeric_ip_literal(const std::string& value);

} // namespace util

#endif
