#include "util/string_utils.hpp"

#include "scan/target_validation.hpp"

namespace util
{

std::string trim(const std::string& s)
{
    const std::string ws = " \t\r\n";
    size_t start = s.find_first_not_of(ws);
    if (start == std::string::npos)
        return "";
    size_t end = s.find_last_not_of(ws);
    return s.substr(start, end - start + 1);
}

bool is_numeric_ip_literal(const std::string& value)
{
    return is_valid_ipv4_literal(value) || is_valid_ipv6_literal(value);
}

} // namespace util
