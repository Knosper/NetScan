#ifndef NMAP_RUNNER_INTERNAL_HPP
#define NMAP_RUNNER_INTERNAL_HPP

#include "scan/nmap_runner.hpp"
#include "util/logger.hpp"
#include <functional>
#include <string>

enum class NmapProgressMode
{
    legacy_mixed = 0,
    timing_text,
    host_discovery_stats,
    host_count_xml,
};

struct NmapOutputWatch
{
    std::string pending_line;
    bool detected_fatal_error = false;
    std::string detected_error;
    Logger* logger = nullptr;
    std::function<void(int)> progress_callback;
    std::function<void(int)> eta_callback;
    std::function<void(int)> hosts_callback;
    NmapProgressMode progress_mode = NmapProgressMode::legacy_mixed;
    int estimated_total_hosts = 0;
    int completed_hosts = 0;
    int last_reported_progress = -1;
    int last_reported_eta = -1;
    int last_reported_hosts = -1;
};

void consume_nmap_output_chunk(NmapOutputWatch& watch, const char* data,
                               std::string::size_type size);
void finish_nmap_output_watch(NmapOutputWatch& watch);

struct ProcessSpec;
NmapProgressMode classify_nmap_progress_mode(const ProcessSpec& process);
int estimate_target_host_count(const ProcessSpec& process);
std::string find_xml_output_path(const ProcessSpec& process);
std::string read_text_file(const std::string& path);

struct OutputWatchConfig
{
    const ProcessSpec* process = nullptr;
    Logger* logger = nullptr;
    NmapProcessListener* listener = nullptr;
};

void initialize_output_watch(NmapOutputWatch& watch, const OutputWatchConfig& config);

#endif
