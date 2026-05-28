#include "scan/nmap_command_builder.hpp"

#include "scan/target_validation.hpp"
#include "util/string_utils.hpp"

#include <cassert>
#include <string>

namespace
{
int clamp_stats_interval_seconds(int seconds)
{
    if (seconds < 1)
        return 1;
    if (seconds > 20)
        return 20;
    return seconds;
}

int select_stats_interval_seconds(const std::string& raw_target)
{
    const std::string target = util::trim(raw_target);
    if (target.empty())
        return 5;

    const std::string::size_type slash = target.find('/');
    if (slash == std::string::npos)
        return is_valid_scan_target_base(target) ? 1 : 5;

    if (slash == 0 || slash != target.rfind('/') || slash + 1 == target.size())
        return 5;

    const std::string base = target.substr(0, slash);
    const std::string prefix = target.substr(slash + 1);
    if (!is_valid_scan_target_with_prefix(base, prefix))
        return 5;

    const int max_prefix_length = base.find(':') != std::string::npos ? 128 : 32;

    int prefix_length = 0;
    if (!parse_canonical_uint_in_range(prefix, 0, max_prefix_length, prefix_length))
        return 5;

    const int host_bits = max_prefix_length - prefix_length;
    if (host_bits <= 8)
        return 1;

    return clamp_stats_interval_seconds(1 + ((host_bits - 8 + 1) / 2));
}

std::string format_stats_interval(int seconds)
{
    return std::to_string(clamp_stats_interval_seconds(seconds)) + "s";
}
} // namespace

NmapCommandBuildResult build_nmap_command(const ScanRequest& request)
{
    assert(!request.target.empty());

    ProcessSpec process;
    process.executable = request.nmap_executable.empty() ? "nmap" : request.nmap_executable;

    if (request.host_discovery_only)
    {
        process.args.push_back("-sn");
    }
    else
    {
        process.args.push_back("-Pn");
        if (!request.ports.empty())
        {
            process.args.push_back("-p");
            process.args.push_back(request.ports);
        }
    }

    process.args.push_back("--stats-every");
    process.args.push_back(format_stats_interval(select_stats_interval_seconds(request.target)));
    process.args.push_back("-v");
    process.args.push_back("-oX");
    process.args.push_back("-");
    process.args.push_back("--");
    process.args.push_back(request.target);

    return {true, process, ""};
}
