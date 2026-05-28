#include "scan/nmap_runner.hpp"

#include "scan/nmap_runner_internal.hpp"
#include "scan/target_validation.hpp"

#include <fstream>

void initialize_output_watch(NmapOutputWatch& watch, const OutputWatchConfig& config)
{
    watch.logger = config.logger;
    watch.progress_mode = classify_nmap_progress_mode(*config.process);
    if (watch.progress_mode == NmapProgressMode::host_count_xml ||
        watch.progress_mode == NmapProgressMode::host_discovery_stats)
    {
        watch.estimated_total_hosts = estimate_target_host_count(*config.process);
    }
    if (!config.listener)
        return;

    watch.progress_callback = [listener = config.listener](int pct) { listener->on_nmap_progress(pct); };
    watch.eta_callback = [listener = config.listener](int s) { listener->on_nmap_eta(s); };
    if (watch.progress_mode == NmapProgressMode::host_count_xml)
        watch.hosts_callback = [listener = config.listener](int n) { listener->on_nmap_hosts_found(n); };
}

#include <climits>
#include <cstdint>
#include <vector>

NmapProgressMode classify_nmap_progress_mode(const ProcessSpec& process)
{
    for (std::vector<std::string>::const_iterator it = process.args.begin();
         it != process.args.end(); ++it)
    {
        if (*it == "-sn")
            return NmapProgressMode::host_discovery_stats;
        if (*it == "-Pn")
            return NmapProgressMode::host_count_xml;
    }

    return NmapProgressMode::legacy_mixed;
}

int estimate_target_host_count(const ProcessSpec& process)
{
    if (process.args.empty())
        return 0;

    const std::string target = process.args.back();
    if (target.empty() || target == "--")
        return 0;

    if (target == "localhost")
        return 1;

    const std::string::size_type slash = target.find('/');
    if (slash == std::string::npos)
        return is_valid_scan_target_base(target) ? 1 : 0;

    const std::string base = target.substr(0, slash);
    const std::string prefix = target.substr(slash + 1);
    if (!is_valid_scan_target_with_prefix(base, prefix))
        return 0;

    if (base.find(':') != std::string::npos)
    {
        int prefix_length = 0;
        if (!parse_canonical_uint_in_range(prefix, 0, 128, prefix_length) || prefix_length != 128)
            return 0;
        return 1;
    }

    int prefix_length = 0;
    if (!parse_canonical_uint_in_range(prefix, 0, 32, prefix_length))
        return 0;

    const std::uint64_t host_count = std::uint64_t{1} << (32 - prefix_length);
    if (host_count == 0 || host_count > static_cast<std::uint64_t>(INT_MAX))
        return 0;

    return static_cast<int>(host_count);
}

std::string find_xml_output_path(const ProcessSpec& process)
{
    for (std::vector<std::string>::size_type i = 0; i + 1 < process.args.size(); ++i)
    {
        if (process.args[i] == "-oX" && process.args[i + 1] != "-")
            return process.args[i + 1];
    }

    return std::string();
}

std::string read_text_file(const std::string& path)
{
    if (path.empty())
        return std::string();

    std::ifstream input(path.c_str(), std::ios::in | std::ios::binary);
    if (!input)
        return std::string();

    return std::string((std::istreambuf_iterator<char>(input)),
                       std::istreambuf_iterator<char>());
}
