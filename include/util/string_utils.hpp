#ifndef UTIL_STRING_UTILS_HPP
#define UTIL_STRING_UTILS_HPP

#include <string>

namespace util
{

std::string trim(const std::string& s);
std::string to_lower_ascii(std::string s);
// ASCII control characters (0x00-0x1F, 0x7F), including line breaks.
bool contains_control_characters(const std::string& value);
bool is_numeric_ip_literal(const std::string& value);

} // namespace util

#endif
