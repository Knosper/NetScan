#include "scan/port_spec_validator.hpp"

#include "util/string_utils.hpp"

#include <cctype>
#include <cstdlib>
#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace
{
constexpr int    MAX_SPEC_LENGTH = 256;
constexpr int    MAX_PORT_COUNT  = 128;
constexpr int    PORT_MIN        = 1;
constexpr int    PORT_MAX        = 65535;

std::vector<std::string> split_by_comma(const std::string& s)
{
    std::vector<std::string> tokens;
    std::string::size_type   start = 0;

    while (start <= s.size())
    {
        std::string::size_type end = s.find(',', start);
        if (end == std::string::npos)
            end = s.size();
        tokens.push_back(s.substr(start, end - start));
        start = end + 1;
    }

    return tokens;
}

bool parse_port_number(const std::string& s, int& out)
{
    if (s.empty())
        return false;

    for (char c : s)
    {
        if (c < '0' || c > '9')
            return false;
    }

    long value = std::strtol(s.c_str(), nullptr, 10);
    if (value < PORT_MIN || value > PORT_MAX)
        return false;

    out = static_cast<int>(value);
    return true;
}

PortSpecValidation validate_token(const std::string& token, std::set<int>& accumulated_ports, std::string& out_normalized_token)
{
    std::string::size_type dash = token.find('-');

    if (dash == std::string::npos)
    {
        int port = 0;
        if (!parse_port_number(token, port))
            return {false, "invalid port token: '" + token + "'", ""};

        accumulated_ports.insert(port);
        out_normalized_token = token;
        return {true, "", ""};
    }

    const std::string start_str = token.substr(0, dash);
    const std::string end_str   = token.substr(dash + 1);

    int start_port = 0;
    int end_port   = 0;

    if (!parse_port_number(start_str, start_port))
        return {false, "invalid port token: '" + token + "'", ""};

    if (!parse_port_number(end_str, end_port))
        return {false, "invalid port token: '" + token + "'", ""};

    if (start_port > end_port)
        return {false, "range start must be <= end in token: '" + token + "'", ""};

    // Count ports for limit check only
    for (int p = start_port; p <= end_port; ++p)
    {
        accumulated_ports.insert(p);
    }
    
    out_normalized_token = start_str + "-" + end_str;
    return {true, "", ""};
}


std::string build_normalized_spec(const std::vector<std::string>& valid_tokens)
{
    std::string result;

    for (size_t i = 0; i < valid_tokens.size(); ++i)
    {
        if (i > 0)
            result += ',';
        result += valid_tokens[i];
    }

    return result;
}

PortCoverageRange parse_coverage_token(const std::string& token)
{
    const std::string::size_type dash = token.find('-');
    if (dash == std::string::npos)
    {
        int port = 0;
        if (!parse_port_number(token, port))
            return {};
        return {port, port};
    }

    int start_port = 0;
    int end_port = 0;
    if (!parse_port_number(token.substr(0, dash), start_port))
        return {};
    if (!parse_port_number(token.substr(dash + 1), end_port))
        return {};

    return {start_port, end_port};
}

std::vector<PortCoverageRange> normalize_ranges(std::vector<PortCoverageRange> ranges)
{
    ranges.erase(std::remove_if(ranges.begin(), ranges.end(),
                                [](const PortCoverageRange& range)
                                { return range.start_port <= 0 || range.end_port <= 0; }),
                 ranges.end());
    if (ranges.empty())
        return ranges;

    std::sort(ranges.begin(), ranges.end(),
              [](const PortCoverageRange& left, const PortCoverageRange& right)
              {
                  if (left.start_port != right.start_port)
                      return left.start_port < right.start_port;
                  return left.end_port < right.end_port;
              });

    std::vector<PortCoverageRange> merged;
    merged.reserve(ranges.size());
    merged.push_back(ranges.front());

    for (std::vector<PortCoverageRange>::size_type i = 1; i < ranges.size(); ++i)
    {
        PortCoverageRange& current = merged.back();
        const PortCoverageRange& next = ranges[i];
        if (next.start_port <= current.end_port + 1)
        {
            current.end_port = std::max(current.end_port, next.end_port);
            continue;
        }

        merged.push_back(next);
    }

    return merged;
}

std::vector<PortCoverageRange> parse_coverage_ranges(const std::string& spec)
{
    std::vector<PortCoverageRange> ranges;
    if (spec.empty())
        return ranges;

    const std::vector<std::string> tokens = split_by_comma(spec);
    ranges.reserve(tokens.size());
    for (const std::string& token : tokens)
    {
        const PortCoverageRange range = parse_coverage_token(util::trim(token));
        if (range.start_port > 0 && range.end_port > 0)
            ranges.push_back(range);
    }

    return normalize_ranges(std::move(ranges));
}
} // namespace

PortSpecValidation validate_port_spec(const std::string& spec)
{
    const std::string trimmed_spec = util::trim(spec);

    if (trimmed_spec.empty())
        return {true, "", ""};

    if (static_cast<int>(trimmed_spec.size()) > MAX_SPEC_LENGTH)
        return {false, "port spec exceeds maximum length of 256 characters", ""};

    const std::vector<std::string> tokens = split_by_comma(trimmed_spec);

    std::set<int> unique_ports;
    std::vector<std::string> valid_tokens;

    for (const std::string& token : tokens)
    {
        const std::string trimmed_token = util::trim(token);
        std::string normalized_token;
        
        PortSpecValidation result = validate_token(trimmed_token, unique_ports, normalized_token);
        if (!result.ok)
            return result;

        valid_tokens.push_back(normalized_token);

        if (unique_ports.size() > MAX_PORT_COUNT)
            return {false, "port spec exceeds maximum of 128 ports", ""};
    }

    const std::string normalized_spec = build_normalized_spec(valid_tokens);

    return {true, "", normalized_spec};
}

ScanPortCoverage derive_scan_port_coverage(const ScanRequest& request)
{
    ScanPortCoverage coverage;
    coverage.known = true;

    if (request.host_discovery_only)
        return coverage;

    if (request.ports.empty())
    {
        coverage.tcp_ranges.push_back({PORT_MIN, PORT_MAX});
        return coverage;
    }

    coverage.tcp_ranges = parse_coverage_ranges(request.ports);
    return coverage;
}
