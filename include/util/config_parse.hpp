#ifndef UTIL_CONFIG_PARSE_HPP
#define UTIL_CONFIG_PARSE_HPP

#include <string>

namespace util
{

// Canonical bool parser for conf.ini values. Strict match against "true".
// Header-only so the Windows launcher can use it without pulling in
// util/string_utils.o and its transitive scan/target_validation deps.
// Caller is responsible for trimming whitespace before passing the value.
inline bool parse_config_bool(const std::string& trimmed_value)
{
    return trimmed_value == "true";
}

} // namespace util

#endif
