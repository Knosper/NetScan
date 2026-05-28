#include "util/time_utils.hpp"

#include <iomanip>
#include <sstream>

namespace util
{

std::string format_utc_time(std::time_t value, const char* format)
{
    std::tm tm_utc;
#ifdef _WIN32
    if (gmtime_s(&tm_utc, &value) != 0)
        return std::string();
#else
    if (gmtime_r(&value, &tm_utc) == nullptr)
        return std::string();
#endif

    char buf[32];
    if (std::strftime(buf, sizeof(buf), format, &tm_utc) == 0)
        return std::string();
    return buf;
}

bool parse_utc_time(const std::string& value, const char* format, std::time_t& out)
{
    std::tm tm_utc = {};
    std::istringstream ss(value);
    ss >> std::get_time(&tm_utc, format);
    if (ss.fail())
        return false;

    tm_utc.tm_isdst = 0;
#ifdef _WIN32
    out = _mkgmtime(&tm_utc);
#else
    out = timegm(&tm_utc);
#endif
    return out != static_cast<std::time_t>(-1);
}

} // namespace util
