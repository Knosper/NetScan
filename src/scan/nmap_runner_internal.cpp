#include "scan/nmap_runner_internal.hpp"
#include "util/logger.hpp"

#include <cstdlib>

namespace
{
constexpr char MSG_INVALID_TARGET[] = "invalid target or no hosts resolved";
constexpr std::string::size_type MAX_PENDING_LINE_BYTES = 64U * 1024U;

std::string::size_type find_line_break(const std::string& value)
{
    const std::string::size_type newline_pos = value.find('\n');
    const std::string::size_type carriage_return_pos = value.find('\r');

    if (newline_pos == std::string::npos)
        return carriage_return_pos;
    if (carriage_return_pos == std::string::npos)
        return newline_pos;
    return newline_pos < carriage_return_pos ? newline_pos : carriage_return_pos;
}

std::string strip_trailing_carriage_return(const std::string& line)
{
    if (!line.empty() && line[line.size() - 1] == '\r')
        return line.substr(0, line.size() - 1);
    return line;
}

bool try_parse_progress(const std::string& line, int& out_progress)
{
    if (line.find("Scan Timing: About ") == std::string::npos)
        return false;

    const std::string::size_type about_pos = line.find("About ");
    if (about_pos == std::string::npos)
        return false;

    const std::string::size_type pct_pos = line.find("% done", about_pos);
    if (pct_pos == std::string::npos)
        return false;

    const std::string::size_type number_start = about_pos + 6;
    if (number_start >= pct_pos)
        return false;

    const std::string number_str = line.substr(number_start, pct_pos - number_start);
    if (number_str.empty())
        return false;

    for (char c : number_str)
    {
        if ((c < '0' || c > '9') && c != '.')
            return false;
    }

    char* end_ptr = nullptr;
    const double pct = std::strtod(number_str.c_str(), &end_ptr);
    if (end_ptr == number_str.c_str() || *end_ptr != '\0')
        return false;
    if (pct < 0.0 || pct > 100.0)
        return false;

    out_progress = static_cast<int>(pct);
    if (out_progress > 99)
        out_progress = 99;
    return true;
}

// Parses "(H:MM:SS remaining)" from a timing line and returns total seconds.
// Returns -1 if the pattern is not found or cannot be parsed.
int try_parse_eta_seconds(const std::string& line)
{
    const std::string::size_type rem_pos = line.find(" remaining)");
    if (rem_pos == std::string::npos)
        return -1;

    const std::string::size_type open_pos = line.rfind('(', rem_pos);
    if (open_pos == std::string::npos || open_pos >= rem_pos)
        return -1;

    const std::string token = line.substr(open_pos + 1, rem_pos - open_pos - 1);

    int h = 0;
    int m = 0;
    int s = 0;
    if (std::sscanf(token.c_str(), "%d:%d:%d", &h, &m, &s) != 3)
        return -1;
    if (h < 0 || m < 0 || s < 0)
        return -1;

    return h * 3600 + m * 60 + s;
}

bool extract_trailing_uint_before(const std::string& line, std::string::size_type end_pos,
                                  long& out_value)
{
    std::string::size_type start = end_pos;
    while (start > 0 && line[start - 1] >= '0' && line[start - 1] <= '9')
        --start;

    if (start == end_pos)
        return false;

    const std::string digits = line.substr(start, end_pos - start);
    char* end_ptr = nullptr;
    const long parsed = std::strtol(digits.c_str(), &end_ptr, 10);
    if (end_ptr == digits.c_str() || *end_ptr != '\0' || parsed < 0)
        return false;

    out_value = parsed;
    return true;
}

bool try_parse_hosts_completed_progress(const std::string& line, int estimated_total_hosts,
                                        int& out_progress)
{
    if (estimated_total_hosts <= 0)
        return false;
    if (line.find("Stats: ") == std::string::npos)
        return false;

    const std::string::size_type marker_pos = line.find(" hosts completed");
    if (marker_pos == std::string::npos || marker_pos == 0)
        return false;

    long completed_hosts = 0;
    if (!extract_trailing_uint_before(line, marker_pos, completed_hosts))
        return false;

    out_progress = static_cast<int>((completed_hosts * 100L) / estimated_total_hosts);
    if (out_progress > 99)
        out_progress = 99;
    return true;
}

bool is_host_element_line(const std::string& line)
{
    const std::string::size_type host_pos = line.find("<host");
    if (host_pos == std::string::npos)
        return false;

    const std::string::size_type next_pos = host_pos + 5;
    return next_pos < line.size() && (line[next_pos] == ' ' || line[next_pos] == '>');
}

void emit_progress(NmapOutputWatch& watch, int progress)
{
    if (!watch.progress_callback)
        return;

    if (progress < 0)
        progress = 0;
    if (progress > 99)
        progress = 99;
    if (progress <= watch.last_reported_progress)
        return;

    watch.last_reported_progress = progress;
    watch.progress_callback(progress);
}

void emit_eta(NmapOutputWatch& watch, int seconds)
{
    if (!watch.eta_callback || seconds < 0)
        return;
    if (seconds == watch.last_reported_eta)
        return;

    watch.last_reported_eta = seconds;
    watch.eta_callback(seconds);
}

void emit_hosts(NmapOutputWatch& watch, int count)
{
    if (!watch.hosts_callback || count < 0)
        return;
    if (count == watch.last_reported_hosts)
        return;

    watch.last_reported_hosts = count;
    watch.hosts_callback(count);
}

bool detect_fatal_nmap_output(NmapOutputWatch& watch, const std::string& line)
{
    if (line.find("Failed to resolve") != std::string::npos ||
        line.find("No targets were specified") != std::string::npos)
    {
        watch.detected_fatal_error = true;
        watch.detected_error = MSG_INVALID_TARGET;
        if (watch.logger)
            watch.logger->warn(std::string("nmap fatal output: ") + line);
        return true;
    }

    return false;
}

void log_nmap_output_line(NmapOutputWatch& watch, const std::string& line)
{
    if (watch.logger && !line.empty())
        watch.logger->debug(std::string("nmap: ") + line);
}

void handle_host_count_progress(NmapOutputWatch& watch, const std::string& line)
{
    const bool use_host_count_progress =
        watch.progress_mode == NmapProgressMode::host_count_xml ||
        (watch.progress_mode == NmapProgressMode::legacy_mixed &&
         watch.estimated_total_hosts > 0);
    if (!use_host_count_progress || watch.estimated_total_hosts <= 0 || !is_host_element_line(line))
        return;

    ++watch.completed_hosts;
    const int progress = (watch.completed_hosts * 100) / watch.estimated_total_hosts;
    emit_progress(watch, progress);
    if (watch.progress_mode == NmapProgressMode::host_count_xml)
        emit_hosts(watch, watch.completed_hosts);
}

bool should_use_timing_progress(const NmapOutputWatch& watch)
{
    return watch.progress_mode == NmapProgressMode::timing_text ||
           watch.progress_mode == NmapProgressMode::host_discovery_stats ||
           watch.progress_mode == NmapProgressMode::host_count_xml ||
           watch.progress_mode == NmapProgressMode::legacy_mixed;
}

void handle_stats_progress(NmapOutputWatch& watch, const std::string& line)
{
    if (watch.progress_mode != NmapProgressMode::host_discovery_stats || !watch.progress_callback)
        return;

    int progress = 0;
    if (try_parse_hosts_completed_progress(line, watch.estimated_total_hosts, progress))
        emit_progress(watch, progress);
}

void handle_timing_progress(NmapOutputWatch& watch, const std::string& line)
{
    if (!should_use_timing_progress(watch))
        return;

    if (watch.progress_callback)
    {
        int progress = 0;
        if (try_parse_progress(line, progress))
            emit_progress(watch, progress);
    }

    emit_eta(watch, try_parse_eta_seconds(line));
}

void consume_nmap_output_line(NmapOutputWatch& watch, const std::string& raw_line)
{
    if (watch.detected_fatal_error)
        return;

    const std::string line = strip_trailing_carriage_return(raw_line);
    log_nmap_output_line(watch, line);
    if (detect_fatal_nmap_output(watch, line))
        return;

    handle_host_count_progress(watch, line);
    handle_stats_progress(watch, line);
    handle_timing_progress(watch, line);
}

void flush_oversized_pending_line(NmapOutputWatch& watch)
{
    if (watch.pending_line.size() <= MAX_PENDING_LINE_BYTES)
        return;

    if (watch.logger)
    {
        watch.logger->warn("nmap output line exceeded buffer; flushing partial line");
    }

    consume_nmap_output_line(watch, watch.pending_line);
    watch.pending_line.clear();
}
} // namespace

void consume_nmap_output_chunk(NmapOutputWatch& watch, const char* data,
                               std::string::size_type size)
{
    watch.pending_line.append(data, size);

    std::string::size_type break_pos = find_line_break(watch.pending_line);
    while (break_pos != std::string::npos)
    {
        const char delimiter = watch.pending_line[break_pos];
        consume_nmap_output_line(watch, watch.pending_line.substr(0, break_pos));
        watch.pending_line.erase(0, break_pos + 1);
        if (delimiter == '\r' && !watch.pending_line.empty() && watch.pending_line[0] == '\n')
            watch.pending_line.erase(0, 1);
        break_pos = find_line_break(watch.pending_line);
    }

    flush_oversized_pending_line(watch);
}

void finish_nmap_output_watch(NmapOutputWatch& watch)
{
    if (watch.pending_line.empty())
        return;

    consume_nmap_output_line(watch, watch.pending_line);
    watch.pending_line.clear();
}
