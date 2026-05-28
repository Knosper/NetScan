#include "scan/nmap_command_builder.hpp"
#include "scan/scan_chunk_planner.hpp"

#include <cstdlib>
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

bool expect_args(const std::vector<std::string>& actual, const std::vector<std::string>& expected,
                 const std::string& label)
{
    if (actual == expected)
        return true;

    std::cerr << "FAIL: " << label << "\n";
    std::cerr << "  expected:";
    for (std::vector<std::string>::size_type i = 0; i < expected.size(); ++i)
        std::cerr << " [" << expected[i] << "]";
    std::cerr << "\n";
    std::cerr << "  actual:";
    for (std::vector<std::string>::size_type i = 0; i < actual.size(); ++i)
        std::cerr << " [" << actual[i] << "]";
    std::cerr << "\n";
    return false;
}

bool expect_single_request(const ScanChunkPlan& plan, const std::string& target,
                           const std::string& label)
{
    if (plan.chunked || plan.requests.size() != 1 || plan.requests[0].target != target)
    {
        std::cerr << "FAIL: " << label << "\n";
        return false;
    }

    return true;
}
} // namespace

int main()
{
    bool all_ok = true;

    {
        ScanRequest request;
        request.target = "192.168.1.0/24";
        request.host_discovery_only = true;

        NmapCommandBuildResult result = build_nmap_command(request);
        all_ok = expect(result.ok, "host discovery request should be accepted") && all_ok;
        all_ok = expect(result.process.executable == "nmap",
                        "host discovery executable should be nmap") &&
                 all_ok;
        all_ok = expect_args(result.process.args,
                             {"-sn", "--stats-every", "1s", "-v", "-oX", "-", "--", "192.168.1.0/24"},
                             "host discovery args") &&
                 all_ok;
    }

    {
        ScanRequest request;
        request.target = "127.0.0.1";
        request.host_discovery_only = false;

        NmapCommandBuildResult result = build_nmap_command(request);
        all_ok = expect(result.ok, "port scan without explicit port should be accepted") && all_ok;
        all_ok = expect_args(result.process.args,
                             {"-Pn", "--stats-every", "1s", "-v", "-oX", "-", "--", "127.0.0.1"},
                             "port scan without port args") &&
                 all_ok;
    }

    {
        ScanRequest request;
        request.target = "localhost";
        request.host_discovery_only = false;
        request.ports = "443";

        NmapCommandBuildResult result = build_nmap_command(request);
        all_ok = expect(result.ok, "port scan with explicit port should be accepted") && all_ok;
        all_ok = expect_args(result.process.args,
                             {"-Pn", "-p", "443", "--stats-every", "1s", "-v", "-oX", "-", "--",
                              "localhost"},
                             "port scan with port args") &&
                 all_ok;
    }

    {
        ScanRequest request;
        request.target = "localhost";
        request.host_discovery_only = false;
        request.ports = "22,80,443,1000-1010";

        NmapCommandBuildResult result = build_nmap_command(request);
        all_ok = expect(result.ok, "port scan with port range should be accepted") && all_ok;
        all_ok = expect_args(result.process.args,
                             {"-Pn", "-p", "22,80,443,1000-1010", "--stats-every", "1s", "-v",
                              "-oX", "-", "--", "localhost"},
                             "port scan with port range args") &&
                 all_ok;
    }

    {
        ScanRequest request;
        request.target = "::1";
        request.host_discovery_only = false;

        NmapCommandBuildResult result = build_nmap_command(request);
        all_ok = expect(result.ok, "port scan without ports should be accepted") && all_ok;
        all_ok = expect_args(result.process.args,
                             {"-Pn", "--stats-every", "1s", "-v", "-oX", "-", "--", "::1"},
                             "port scan without ports args") &&
                 all_ok;
    }

    {
        ScanRequest request;
        request.target = "::1";
        request.host_discovery_only = true;

        NmapCommandBuildResult result = build_nmap_command(request);
        all_ok = expect(result.ok, "IPv6 loopback host discovery should be accepted") && all_ok;
        all_ok = expect_args(result.process.args,
                             {"-sn", "--stats-every", "1s", "-v", "-oX", "-", "--", "::1"},
                             "IPv6 loopback host discovery args") &&
                 all_ok;
    }

    {
        ScanRequest request;
        request.target = "::1/128";
        request.host_discovery_only = true;

        NmapCommandBuildResult result = build_nmap_command(request);
        all_ok = expect(result.ok, "IPv6 /128 CIDR should be accepted") && all_ok;
        all_ok = expect_args(result.process.args,
                             {"-sn", "--stats-every", "1s", "-v", "-oX", "-", "--", "::1/128"},
                             "IPv6 /128 CIDR args") &&
                 all_ok;
    }

    {
        ScanRequest request;
        request.target = "192.168.1.0/31";
        request.host_discovery_only = true;

        NmapCommandBuildResult result = build_nmap_command(request);
        all_ok = expect(result.ok, "IPv4 /31 CIDR should be accepted") && all_ok;
        all_ok = expect_args(result.process.args,
                             {"-sn", "--stats-every", "1s", "-v", "-oX", "-", "--",
                              "192.168.1.0/31"},
                             "IPv4 /31 CIDR args") &&
                 all_ok;
    }

    {
        ScanRequest request;
        request.target = "192.168.0.0/23";
        request.host_discovery_only = true;

        NmapCommandBuildResult result = build_nmap_command(request);
        all_ok = expect(result.ok, "IPv4 /23 CIDR should be accepted") && all_ok;
        all_ok = expect_args(result.process.args,
                             {"-sn", "--stats-every", "2s", "-v", "-oX", "-", "--",
                              "192.168.0.0/23"},
                             "IPv4 /23 CIDR args") &&
                 all_ok;
    }

    {
        ScanRequest request;
        request.target = "10.0.0.0/16";
        request.host_discovery_only = true;

        NmapCommandBuildResult result = build_nmap_command(request);
        all_ok = expect(result.ok, "IPv4 /16 CIDR should be accepted") && all_ok;
        all_ok = expect_args(result.process.args,
                             {"-sn", "--stats-every", "5s", "-v", "-oX", "-", "--",
                              "10.0.0.0/16"},
                             "IPv4 /16 CIDR args") &&
                 all_ok;
    }

    {
        ScanRequest request;
        request.target = "10.0.0.0/0";
        request.host_discovery_only = true;

        NmapCommandBuildResult result = build_nmap_command(request);
        all_ok = expect(result.ok, "IPv4 /0 CIDR should be accepted") && all_ok;
        all_ok = expect_args(result.process.args,
                             {"-sn", "--stats-every", "13s", "-v", "-oX", "-", "--",
                              "10.0.0.0/0"},
                             "IPv4 /0 CIDR args") &&
                 all_ok;
    }

    {
        ScanRequest request;
        request.target = "fd00::/112";
        request.host_discovery_only = true;

        NmapCommandBuildResult result = build_nmap_command(request);
        all_ok = expect(result.ok, "IPv6 /112 CIDR should be accepted") && all_ok;
        all_ok = expect_args(result.process.args,
                             {"-sn", "--stats-every", "5s", "-v", "-oX", "-", "--",
                              "fd00::/112"},
                             "IPv6 /112 CIDR args") &&
                 all_ok;
    }

    {
        ScanRequest request;
        request.target = "192.168.1.0/24";
        request.host_discovery_only = false;
        request.ports = "22,80,443";

        all_ok = expect(scan_chunk_count(request) == 8,
                        "IPv4 /24 should report eight planned chunks") &&
                 all_ok;
        all_ok = expect(scan_chunk_count(request) <= max_scan_chunk_count(),
                        "IPv4 /24 should stay within the chunk count policy") &&
                 all_ok;

        ScanChunkPlan plan = plan_scan_chunks(request);
        all_ok = expect(plan.chunked, "large IPv4 port scan should be chunked") && all_ok;
        all_ok = expect(plan.requests.size() == 8, "IPv4 /24 should produce eight /27 chunks") &&
                 all_ok;
        if (plan.requests.size() == 8)
        {
            all_ok = expect(plan.requests[0].target == "192.168.1.0/27",
                            "first /24 chunk should start at network address") &&
                     all_ok;
            all_ok = expect(plan.requests[7].target == "192.168.1.224/27",
                            "last /24 chunk should stay inside original target") &&
                     all_ok;
            all_ok = expect(plan.requests[3].ports == request.ports,
                            "chunk should keep requested port spec") &&
                     all_ok;
        }

        if (!plan.requests.empty())
        {
            NmapCommandBuildResult result = build_nmap_command(plan.requests[0]);
            all_ok = expect_args(result.process.args,
                                 {"-Pn", "-p", "22,80,443", "--stats-every", "1s", "-v",
                                  "-oX", "-", "--", "192.168.1.0/27"},
                                 "chunked IPv4 port scan args") &&
                     all_ok;
        }
    }

    {
        ScanRequest request;
        request.target = "192.168.1.64/26";
        request.host_discovery_only = false;

        ScanChunkPlan plan = plan_scan_chunks(request);
        all_ok = expect(plan.chunked, "IPv4 /26 port scan should be chunked") && all_ok;
        all_ok = expect(plan.requests.size() == 2, "IPv4 /26 should produce two /27 chunks") &&
                 all_ok;
        if (plan.requests.size() == 2)
        {
            all_ok = expect(plan.requests[0].target == "192.168.1.64/27",
                            "first /26 chunk should preserve CIDR network") &&
                     all_ok;
            all_ok = expect(plan.requests[1].target == "192.168.1.96/27",
                            "second /26 chunk should preserve CIDR network") &&
                     all_ok;
        }
    }

    {
        ScanRequest request;
        request.target = "10.0.0.0/16";
        request.host_discovery_only = false;
        request.ports = "22";

        all_ok = expect(scan_chunk_count(request) == 2048,
                        "IPv4 /16 should report the computed /27 chunk count") &&
                 all_ok;
        all_ok = expect(scan_chunk_count(request) > max_scan_chunk_count(),
                        "IPv4 /16 should exceed the chunk count policy") &&
                 all_ok;
    }

    {
        ScanRequest request;
        request.target = "192.168.1.0/27";
        request.host_discovery_only = false;

        all_ok = expect_single_request(plan_scan_chunks(request), "192.168.1.0/27",
                                       "IPv4 /27 should stay on single scan path") &&
                 all_ok;
    }

    {
        ScanRequest request;
        request.target = "192.168.1.0/24";
        request.host_discovery_only = true;

        all_ok = expect_single_request(plan_scan_chunks(request), "192.168.1.0/24",
                                       "host discovery should stay on single scan path") &&
                 all_ok;
    }

    {
        ScanRequest request;
        request.target = "fd00::/112";
        request.host_discovery_only = false;

        all_ok = expect_single_request(plan_scan_chunks(request), "fd00::/112",
                                       "IPv6 target should stay on single scan path") &&
                 all_ok;
    }

    return finish_test("nmap_command_builder_test", all_ok);
}
