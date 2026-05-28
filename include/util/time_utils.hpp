#ifndef UTIL_TIME_UTILS_HPP
#define UTIL_TIME_UTILS_HPP

#include <ctime>
#include <string>

namespace util
{

std::string format_utc_time(std::time_t value, const char* format);
bool parse_utc_time(const std::string& value, const char* format, std::time_t& out);

} // namespace util

#endif
