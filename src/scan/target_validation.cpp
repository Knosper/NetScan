#include "scan/target_validation.hpp"

#include "util/string_utils.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

bool is_ascii_digits(const std::string& value)
{
    if (value.empty())
        return false;

    for (char c : value)
    {
        if (!std::isdigit(static_cast<unsigned char>(c)))
            return false;
    }

    return true;
}

bool parse_uint_in_range(const std::string& value, int min_value, int max_value)
{
    if (!is_ascii_digits(value))
        return false;

    int parsed = 0;
    for (char c : value)
    {
        parsed = parsed * 10 + (c - '0');
        if (parsed > max_value)
            return false;
    }

    return parsed >= min_value;
}

bool is_canonical_uint(const std::string& value)
{
    if (!is_ascii_digits(value))
        return false;

    return value == "0" || value[0] != '0';
}

bool parse_canonical_uint_in_range(const std::string& value, int min_value, int max_value,
                                   int& parsed)
{
    if (!is_canonical_uint(value))
        return false;

    parsed = 0;
    for (char c : value)
    {
        parsed = parsed * 10 + (c - '0');
        if (parsed > max_value)
            return false;
    }

    return parsed >= min_value;
}

bool looks_like_ipv4_literal(const std::string& target)
{
    bool has_dot = false;
    for (char c : target)
    {
        if (std::isdigit(static_cast<unsigned char>(c)))
            continue;
        if (c == '.')
        {
            has_dot = true;
            continue;
        }
        return false;
    }

    return has_dot;
}

// Returns true and fills address (host byte order) when target is a valid,
// canonical IPv4 literal (no leading zeros in octets).
bool parse_ipv4_literal(const std::string& target, std::uint32_t& address)
{
    struct in_addr addr4;
    if (inet_pton(AF_INET, target.c_str(), &addr4) != 1)
        return false;

    char canonical[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &addr4, canonical, sizeof(canonical)) == nullptr)
        return false;

    if (target != canonical)
        return false;

    address = ntohl(addr4.s_addr);
    return true;
}

bool is_valid_ipv4_literal(const std::string& target)
{
    std::uint32_t unused = 0;
    return parse_ipv4_literal(target, unused);
}

// Fills groups[8] with the 16-bit groups in host byte order from an IPv6
// literal accepted by inet_pton.
bool parse_ipv6_literal(const std::string& target, std::uint16_t groups[8])
{
    struct in6_addr addr6;
    if (inet_pton(AF_INET6, target.c_str(), &addr6) != 1)
        return false;

    for (int i = 0; i < 8; ++i)
    {
        groups[i] = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(addr6.s6_addr[2 * i]) << 8) |
            static_cast<std::uint16_t>(addr6.s6_addr[2 * i + 1]));
    }
    return true;
}

bool is_valid_ipv6_literal(const std::string& target)
{
    std::uint16_t unused[8] = {};
    return parse_ipv6_literal(target, unused);
}

bool ipv4_is_within_allowed_block(std::uint32_t address, int prefix_length)
{
    struct AllowedIpv4Block
    {
        std::uint32_t network;
        int prefix_length;
    };

    static const AllowedIpv4Block kAllowedBlocks[] = {
        {0x7f000000u, 8},
        {0x0a000000u, 8},
        {0xac100000u, 12},
        {0xc0a80000u, 16},
    };

    for (const AllowedIpv4Block& block : kAllowedBlocks)
    {
        if (prefix_length < block.prefix_length)
            continue;

        const std::uint32_t mask =
            block.prefix_length == 0 ? 0u : (0xffffffffu << (32 - block.prefix_length));
        if ((address & mask) == block.network)
            return true;
    }

    return false;
}

bool ipv6_matches_prefix(const std::uint16_t address[8], const std::uint16_t network[8],
                         int prefix_length)
{
    const int full_groups = prefix_length / 16;
    const int remaining_bits = prefix_length % 16;

    for (int i = 0; i < full_groups; ++i)
    {
        if (address[i] != network[i])
            return false;
    }

    if (remaining_bits == 0)
        return true;

    const std::uint16_t mask = static_cast<std::uint16_t>(0xffffu << (16 - remaining_bits));
    return (address[full_groups] & mask) == (network[full_groups] & mask);
}

bool ipv6_is_within_allowed_block(const std::uint16_t address[8], int prefix_length)
{
    struct AllowedIpv6Block
    {
        std::uint16_t network[8];
        int prefix_length;
    };

    static const AllowedIpv6Block kAllowedBlocks[] = {
        {{0, 0, 0, 0, 0, 0, 0, 1}, 128},
        {{0xfe80, 0, 0, 0, 0, 0, 0, 0}, 10},
        {{0xfc00, 0, 0, 0, 0, 0, 0, 0}, 7},
    };

    for (const AllowedIpv6Block& block : kAllowedBlocks)
    {
        if (prefix_length < block.prefix_length)
            continue;

        if (ipv6_matches_prefix(address, block.network, block.prefix_length))
            return true;
    }

    return false;
}

bool is_allowed_scan_target_base(const std::string& target)
{
    if (target == "localhost")
        return true;

    if (target.find(':') != std::string::npos)
    {
        std::uint16_t groups[8] = {};
        return parse_ipv6_literal(target, groups) && ipv6_is_within_allowed_block(groups, 128);
    }

    if (looks_like_ipv4_literal(target))
    {
        std::uint32_t address = 0;
        return parse_ipv4_literal(target, address) && ipv4_is_within_allowed_block(address, 32);
    }

    return false;
}

bool is_allowed_scan_target_with_prefix(const std::string& base, const std::string& prefix)
{
    if (!is_canonical_uint(prefix))
        return false;

    if (base.find(':') != std::string::npos)
    {
        std::uint16_t groups[8] = {};
        int prefix_length = 0;
        return parse_ipv6_literal(base, groups) &&
               parse_canonical_uint_in_range(prefix, 0, 128, prefix_length) &&
               ipv6_is_within_allowed_block(groups, prefix_length);
    }

    if (looks_like_ipv4_literal(base))
    {
        std::uint32_t address = 0;
        int prefix_length = 0;
        return parse_ipv4_literal(base, address) &&
               parse_canonical_uint_in_range(prefix, 0, 32, prefix_length) &&
               ipv4_is_within_allowed_block(address, prefix_length);
    }

    return false;
}

bool is_valid_scan_target_base(const std::string& target)
{
    if (target == "localhost")
        return true;

    if (target.find(':') != std::string::npos)
        return is_valid_ipv6_literal(target);

    if (looks_like_ipv4_literal(target))
        return is_valid_ipv4_literal(target);

    return false;
}

bool is_valid_scan_target_with_prefix(const std::string& base, const std::string& prefix)
{
    if (!is_valid_scan_target_base(base) || !is_canonical_uint(prefix))
        return false;

    if (base.find(':') != std::string::npos)
        return parse_uint_in_range(prefix, 0, 128);

    const bool is_ipv4 = looks_like_ipv4_literal(base);
    return is_ipv4 && parse_uint_in_range(prefix, 0, 32);
}

bool try_parse_ip_or_cidr(const std::string& raw, ParsedCidr& out)
{
    const std::string value = util::trim(raw);
    const std::string::size_type slash = value.find('/');
    if (slash != std::string::npos && (slash == 0 || slash != value.rfind('/') || slash + 1 == value.size()))
        return false;

    const std::string base = slash == std::string::npos ? value : value.substr(0, slash);
    const std::string prefix = slash == std::string::npos ? "" : value.substr(slash + 1);

    if (base.find(':') != std::string::npos)
    {
        if (!parse_ipv6_literal(base, out.ipv6))
            return false;
        out.is_ipv4 = false;
        out.prefix_length = 128;
        if (!prefix.empty() && !parse_canonical_uint_in_range(prefix, 0, 128, out.prefix_length))
            return false;
        return true;
    }

    if (!parse_ipv4_literal(base, out.ipv4))
        return false;
    out.is_ipv4 = true;
    out.prefix_length = 32;
    if (!prefix.empty() && !parse_canonical_uint_in_range(prefix, 0, 32, out.prefix_length))
        return false;
    return true;
}

namespace
{
bool cidr_contains(const ParsedCidr& outer, const ParsedCidr& inner)
{
    if (outer.is_ipv4 != inner.is_ipv4)
        return false;
    if (inner.prefix_length < outer.prefix_length)
        return false;

    if (outer.is_ipv4)
    {
        const std::uint32_t mask = outer.prefix_length == 0
                                       ? 0u
                                       : (0xffffffffu << (32 - outer.prefix_length));
        return (outer.ipv4 & mask) == (inner.ipv4 & mask);
    }

    return ipv6_matches_prefix(inner.ipv6, outer.ipv6, outer.prefix_length);
}
} // namespace

bool is_user_target_allowed(const std::string& target,
                            const std::vector<std::string>& user_allowed_targets)
{
    ParsedCidr requested;
    if (!try_parse_ip_or_cidr(target, requested))
        return false;

    for (std::size_t i = 0; i < user_allowed_targets.size(); ++i)
    {
        ParsedCidr allowed;
        if (!try_parse_ip_or_cidr(user_allowed_targets[i], allowed))
            continue;
        if (cidr_contains(allowed, requested))
            return true;
    }
    return false;
}

bool is_valid_scan_target(const std::string& target)
{
    const std::string trimmed = util::trim(target);

    if (trimmed.empty())
        return false;

    if (trimmed[0] == '-')
        return false;

    const std::string::size_type slash = trimmed.find('/');
    if (slash == std::string::npos)
        return is_allowed_scan_target_base(trimmed);

    if (slash == 0 || slash != trimmed.rfind('/'))
        return false;

    const std::string base = trimmed.substr(0, slash);
    const std::string prefix = trimmed.substr(slash + 1);
    return is_allowed_scan_target_with_prefix(base, prefix);
}

namespace
{

std::string canonicalize_ipv6_base(const std::string& target)
{
    struct in6_addr addr6;
    if (inet_pton(AF_INET6, target.c_str(), &addr6) != 1)
        return target;

    char canonical[INET6_ADDRSTRLEN];
    if (inet_ntop(AF_INET6, &addr6, canonical, sizeof(canonical)) == nullptr)
        return target;

    return canonical;
}

std::string lowercase(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

std::string canonicalize_target(const std::string& target)
{
    const std::string trimmed = util::trim(target);
    const std::string::size_type slash = trimmed.find('/');
    const std::string base = slash == std::string::npos ? trimmed : trimmed.substr(0, slash);
    const std::string prefix = slash == std::string::npos ? "" : trimmed.substr(slash);

    std::string canonical_base;
    if (base.find(':') != std::string::npos)
        canonical_base = canonicalize_ipv6_base(base);
    else if (looks_like_ipv4_literal(base))
        canonical_base = base;
    else
        canonical_base = lowercase(base);

    return canonical_base + prefix;
}
