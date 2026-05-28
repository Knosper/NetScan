#ifndef SCAN_TARGET_VALIDATION_HPP
#define SCAN_TARGET_VALIDATION_HPP

#include <cstdint>
#include <string>
#include <vector>

bool is_ascii_digits(const std::string& value);
bool parse_uint_in_range(const std::string& value, int min_value, int max_value);
bool is_canonical_uint(const std::string& value);
bool parse_canonical_uint_in_range(const std::string& value, int min_value, int max_value,
                                   int& parsed);
bool looks_like_ipv4_literal(const std::string& target);
bool is_valid_ipv4_literal(const std::string& target);
bool parse_ipv4_literal(const std::string& target, std::uint32_t& address);
bool is_valid_ipv6_literal(const std::string& target);
bool parse_ipv6_literal(const std::string& target, std::uint16_t groups[8]);
bool ipv4_is_within_allowed_block(std::uint32_t address, int prefix_length);
bool ipv6_matches_prefix(const std::uint16_t address[8], const std::uint16_t network[8],
                         int prefix_length);
bool ipv6_is_within_allowed_block(const std::uint16_t address[8], int prefix_length);
bool is_allowed_scan_target_base(const std::string& target);
bool is_allowed_scan_target_with_prefix(const std::string& base, const std::string& prefix);
bool is_valid_scan_target_base(const std::string& target);
bool is_valid_scan_target_with_prefix(const std::string& base, const std::string& prefix);
bool is_valid_scan_target(const std::string& target);

struct ParsedCidr
{
    bool is_ipv4 = false;
    std::uint32_t ipv4 = 0;
    std::uint16_t ipv6[8] = {};
    int prefix_length = 0;
};

bool try_parse_ip_or_cidr(const std::string& raw, ParsedCidr& out);

std::string canonicalize_target(const std::string& target);

bool is_user_target_allowed(const std::string& target,
                            const std::vector<std::string>& user_allowed_targets);

#endif
