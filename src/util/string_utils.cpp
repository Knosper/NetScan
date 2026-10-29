#include "util/string_utils.hpp"

#include "scan/target_validation.hpp"

#include <algorithm>
#include <cctype>

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

std::string to_lower_ascii(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool contains_control_characters(const std::string& value)
{
    for (std::string::const_iterator it = value.begin(); it != value.end(); ++it)
    {
        const unsigned char ch = static_cast<unsigned char>(*it);
        if (ch < 0x20 || ch == 0x7F)
            return true;
    }
    return false;
}

bool is_numeric_ip_literal(const std::string& value)
{
    return is_valid_ipv4_literal(value) || is_valid_ipv6_literal(value);
}

} // namespace util
