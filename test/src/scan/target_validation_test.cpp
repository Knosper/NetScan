#include "scan/target_validation.hpp"

#include <iostream>
#include <string>
#include <vector>
#include "test_output.hpp"

namespace
{
bool expect(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << "\n";
        return false;
    }
    return true;
}
} // namespace

int main()
{
    bool all_ok = true;

    {
        const std::vector<std::string> allowed;
        all_ok = expect(!is_user_target_allowed("192.168.1.1", allowed),
                        "empty allow-list must reject any target") && all_ok;
    }

    {
        const std::vector<std::string> allowed = {"192.168.1.1"};
        all_ok = expect(is_user_target_allowed("192.168.1.1", allowed),
                        "exact IPv4 address must be allowed when it matches the allow-list entry") && all_ok;
    }

    {
        const std::vector<std::string> allowed = {"192.168.1.1"};
        all_ok = expect(!is_user_target_allowed("192.168.1.2", allowed),
                        "IPv4 address must be rejected when it does not match the allow-list entry") && all_ok;
    }

    {
        const std::vector<std::string> allowed = {"192.168.1.0/24"};
        all_ok = expect(is_user_target_allowed("192.168.1.42", allowed),
                        "IPv4 address inside allowed /24 must be allowed") && all_ok;
    }

    {
        const std::vector<std::string> allowed = {"192.168.1.0/24"};
        all_ok = expect(!is_user_target_allowed("192.168.2.1", allowed),
                        "IPv4 address outside allowed /24 must be rejected") && all_ok;
    }

    {
        const std::vector<std::string> allowed = {"10.0.0.0/8"};
        all_ok = expect(is_user_target_allowed("10.1.2.0/24", allowed),
                        "narrower CIDR target contained in allowed /8 must be allowed") && all_ok;
    }

    {
        const std::vector<std::string> allowed = {"10.1.2.0/24"};
        all_ok = expect(!is_user_target_allowed("10.1.0.0/16", allowed),
                        "broader CIDR target that encompasses the allowed entry must be rejected") && all_ok;
    }

    {
        const std::vector<std::string> allowed = {"10.0.0.0/8"};
        all_ok = expect(is_user_target_allowed("10.0.0.0", allowed),
                        "network boundary address (first) must be allowed") && all_ok;
    }

    {
        const std::vector<std::string> allowed = {"10.0.0.0/8"};
        all_ok = expect(is_user_target_allowed("10.255.255.255", allowed),
                        "network boundary address (last) must be allowed") && all_ok;
    }

    {
        const std::vector<std::string> allowed = {"10.0.0.0/8"};
        all_ok = expect(!is_user_target_allowed("11.0.0.0", allowed),
                        "address just beyond allowed /8 boundary must be rejected") && all_ok;
    }

    {
        const std::vector<std::string> allowed = {"192.168.0.0/16"};
        all_ok = expect(!is_user_target_allowed("not-an-ip", allowed),
                        "invalid target string must be rejected") && all_ok;
    }

    {
        const std::vector<std::string> allowed = {"bad-entry", "192.168.1.0/24"};
        all_ok = expect(is_user_target_allowed("192.168.1.10", allowed),
                        "invalid allow-list entry is skipped and valid entry still allows the target") && all_ok;
    }

    {
        const std::vector<std::string> allowed = {"fd00::1/64"};
        all_ok = expect(!is_user_target_allowed("192.168.1.1", allowed),
                        "IPv4 target must be rejected when allow-list contains only IPv6 entries") && all_ok;
    }

    {
        const std::vector<std::string> allowed = {"fd00::1"};
        all_ok = expect(is_user_target_allowed("fd00::1", allowed),
                        "exact IPv6 address must be allowed when it matches the allow-list entry") && all_ok;
    }

    {
        const std::vector<std::string> allowed = {"fd00::/16"};
        all_ok = expect(is_user_target_allowed("fd00::42", allowed),
                        "IPv6 address inside allowed /16 prefix must be allowed") && all_ok;
    }

    {
        const std::vector<std::string> allowed = {"fd00::/16"};
        all_ok = expect(!is_user_target_allowed("fe80::1", allowed),
                        "IPv6 address outside allowed /16 prefix must be rejected") && all_ok;
    }

    all_ok = expect(is_valid_scan_target("localhost"),
                    "localhost must be a valid scan target") && all_ok;

    all_ok = expect(is_valid_scan_target("192.168.1.1"),
                    "private IPv4 address must be valid") && all_ok;

    all_ok = expect(is_valid_scan_target("10.0.0.0/8"),
                    "private IPv4 CIDR /8 must be valid") && all_ok;

    all_ok = expect(is_valid_scan_target("192.168.1.0/24"),
                    "private IPv4 CIDR /24 must be valid") && all_ok;

    all_ok = expect(!is_valid_scan_target(""),
                    "empty string must be rejected") && all_ok;

    all_ok = expect(!is_valid_scan_target("-scanme.nmap.org"),
                    "target starting with dash must be rejected") && all_ok;

    all_ok = expect(!is_valid_scan_target("192.168.1.1;id"),
                    "target with shell metacharacter must be rejected") && all_ok;

    all_ok = expect(!is_valid_scan_target("8.8.8.8"),
                    "public IPv4 address must be rejected") && all_ok;

    all_ok = expect(!is_valid_scan_target("192.168.1.300"),
                    "IPv4 with out-of-range octet must be rejected") && all_ok;

    all_ok = expect(!is_valid_scan_target("192.168.1.0/33"),
                    "IPv4 CIDR with prefix >32 must be rejected") && all_ok;

    all_ok = expect(!is_valid_scan_target("192.168.1.0/24/8"),
                    "target with double slash must be rejected") && all_ok;

    all_ok = expect(!is_valid_scan_target("/24"),
                    "target starting with slash must be rejected") && all_ok;

    all_ok = expect(!is_valid_scan_target("   "),
                    "whitespace-only string must be rejected") && all_ok;

    all_ok = expect(is_valid_scan_target("  192.168.1.1  "),
                    "target with surrounding whitespace must be accepted after trim") && all_ok;

    return finish_test("target_validation_test", all_ok);
}
