#include "scan/nmap_runner.hpp"
#include "scan/nmap_runner_internal.hpp"

#include <cstdlib>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#ifndef _WIN32
#include <unistd.h>
#endif
#ifdef __linux__
#include <dirent.h>
#endif
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

ProcessSpec make_shell_process(const std::string& command)
{
    ProcessSpec process;
#ifdef _WIN32
    process.executable = "cmd.exe";
    process.args.push_back("/C");
    process.args.push_back(command);
#else
    process.executable = "sh";
    process.args.push_back("-c");
    process.args.push_back(command);
#endif
    return process;
}

std::string fatal_shell_command()
{
#ifdef _WIN32
    return "echo Failed to resolve target";
#else
    return "printf 'Failed to resolve target\\n'";
#endif
}

std::string success_shell_command()
{
#ifdef _WIN32
    return "echo scan ok";
#else
    return "printf 'scan ok\\n'";
#endif
}

std::string long_running_shell_command()
{
#ifdef _WIN32
    return "ping -n 30 127.0.0.1 > NUL";
#else
    return "trap '' TERM; while :; do sleep 1; done";
#endif
}

#ifndef _WIN32
std::string make_temp_path(const std::string& suffix)
{
    std::string path_template = "/tmp/netscan-runner-test-XXXXXX" + suffix;
    std::vector<char> buffer(path_template.begin(), path_template.end());
    buffer.push_back('\0');
    const int fd = mkstemps(buffer.data(), static_cast<int>(suffix.size()));
    if (fd >= 0)
        close(fd);
    return std::string(buffer.data());
}

std::string read_file(const std::string& path)
{
    std::ifstream input(path.c_str(), std::ios::in | std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(input)),
                       std::istreambuf_iterator<char>());
}
#endif

class CapturingListener : public NmapProcessListener
{
public:
    void on_nmap_process_started(const NmapProcessHandle& value) override
    {
        std::lock_guard<std::mutex> lock(mutex);
        handle = value;
        started = true;
        cv.notify_all();
    }

    void on_nmap_process_finished() override
    {
        std::lock_guard<std::mutex> lock(mutex);
        finished = true;
        cv.notify_all();
    }

    bool wait_for_start(int timeout_ms)
    {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                           [this]() { return started; });
    }

    bool wait_for_finish(int timeout_ms)
    {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                           [this]() { return finished; });
    }

    NmapProcessHandle current_handle()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return handle;
    }

private:
    std::mutex mutex;
    std::condition_variable cv;
    NmapProcessHandle handle;
    bool started = false;
    bool finished = false;
};

class ProgressCapturingListener : public NmapProcessListener
{
public:
    void on_nmap_process_started(const NmapProcessHandle&) override {}
    void on_nmap_process_finished() override {}

    void on_nmap_progress(int percent) override
    {
        progress_values.push_back(percent);
    }

    std::vector<int> progress_values;
};

ProcessSpec make_target_process(const std::string& flag, const std::string& target)
{
    ProcessSpec process;
    process.executable = "nmap";
    process.args.push_back(flag);
    process.args.push_back("--");
    process.args.push_back(target);
    return process;
}
} // namespace

int main()
{
    bool all_ok = true;

    {
        NmapOutputWatch watch;
        const std::string line = "Failed to resolve host\n";
        consume_nmap_output_chunk(watch, line.c_str(), line.size());
        finish_nmap_output_watch(watch);
        all_ok = expect(watch.detected_fatal_error,
                        "fatal resolver line should be detected") && all_ok;
        all_ok = expect(watch.detected_error == "invalid target or no hosts resolved",
                        "resolver line should map to the expected error") && all_ok;
    }

    {
        NmapOutputWatch watch;
        const std::string chunk_a = "No targets ";
        const std::string chunk_b = "were specified\n";
        consume_nmap_output_chunk(watch, chunk_a.c_str(), chunk_a.size());
        consume_nmap_output_chunk(watch, chunk_b.c_str(), chunk_b.size());
        finish_nmap_output_watch(watch);
        all_ok = expect(watch.detected_fatal_error,
                        "fatal no-target line should be detected across chunks") && all_ok;
    }

    {
        NmapOutputWatch watch;
        const std::string chunk_a = "Nmap done: 0 IP addresses (0 hosts up)\nFailed to res";
        const std::string chunk_b = "olve host\n";
        consume_nmap_output_chunk(watch, chunk_a.c_str(), chunk_a.size());
        all_ok = expect(!watch.detected_fatal_error,
                        "fatal resolver line should wait for the full line across chunks") &&
                 all_ok;
        consume_nmap_output_chunk(watch, chunk_b.c_str(), chunk_b.size());
        finish_nmap_output_watch(watch);
        all_ok = expect(watch.detected_fatal_error,
                        "fatal resolver line should be detected across chunks") && all_ok;
        all_ok = expect(watch.detected_error == "invalid target or no hosts resolved",
                        "split resolver line should map to the expected error") && all_ok;
    }

    {
        NmapOutputWatch watch;
        const std::string output = "Starting Nmap\nAll good\n";
        consume_nmap_output_chunk(watch, output.c_str(), output.size());
        finish_nmap_output_watch(watch);
        all_ok = expect(!watch.detected_fatal_error,
                        "non-fatal output should not trip the detector") && all_ok;
        all_ok = expect(watch.detected_error.empty(),
                        "non-fatal output should not set an error message") && all_ok;
    }

    {
        NmapOutputWatch watch;
        const std::string large_chunk(70U * 1024U, 'x');
        consume_nmap_output_chunk(watch, large_chunk.c_str(), large_chunk.size());
        all_ok = expect(watch.pending_line.empty(),
                        "oversized newline-free output should be flushed to cap memory growth") &&
                 all_ok;
        all_ok = expect(!watch.detected_fatal_error,
                        "oversized newline-free output should not be treated as fatal by itself") &&
                 all_ok;
    }

    {
        ProgressCapturingListener listener;
        NmapOutputWatch watch;
        watch.progress_mode = NmapProgressMode::host_count_xml;
        watch.estimated_total_hosts = 4;
        watch.progress_callback = [&listener](int percent) { listener.on_nmap_progress(percent); };

        const std::string xml_output =
            "<host starttime=\"1\" endtime=\"2\"><status state=\"up\"/></host>\n"
            "<host starttime=\"2\" endtime=\"3\"><status state=\"up\"/></host>\n";
        consume_nmap_output_chunk(watch, xml_output.c_str(), xml_output.size());
        finish_nmap_output_watch(watch);

        all_ok = expect(listener.progress_values.size() == 2,
                        "xml host output should emit incremental host-count progress callbacks") &&
                 all_ok;
        if (listener.progress_values.size() == 2)
        {
            all_ok = expect(listener.progress_values[0] == 25,
                            "first xml host output should map to 25 percent") &&
                     all_ok;
            all_ok = expect(listener.progress_values[1] == 50,
                            "second xml host output should map to 50 percent") &&
                     all_ok;
        }
    }

    {
        ProgressCapturingListener listener;
        NmapOutputWatch watch;
        watch.progress_mode = NmapProgressMode::host_discovery_stats;
        watch.progress_callback = [&listener](int percent) { listener.on_nmap_progress(percent); };

        const std::string progress_line =
            "Parallel DNS resolution of 256 hosts. Timing: About 99.61% done; ETC: 12:00 (0:00:00 remaining)\n";
        consume_nmap_output_chunk(watch, progress_line.c_str(), progress_line.size());
        finish_nmap_output_watch(watch);

        all_ok = expect(listener.progress_values.empty(),
                        "dns resolution timing output should not be treated as scan progress") &&
                 all_ok;
    }

    {
        ProgressCapturingListener listener;
        NmapOutputWatch watch;
        watch.progress_mode = NmapProgressMode::host_discovery_stats;
        watch.estimated_total_hosts = 4;
        watch.progress_callback = [&listener](int percent) { listener.on_nmap_progress(percent); };

        const std::string stats_line =
            "Stats: 0:00:05 elapsed; 2 hosts completed (0 up), 1 undergoing Ping Scan\n";
        consume_nmap_output_chunk(watch, stats_line.c_str(), stats_line.size());
        finish_nmap_output_watch(watch);

        all_ok = expect(listener.progress_values.size() == 1,
                        "host-discovery stats output should emit one parsed progress callback") &&
                 all_ok;
        if (listener.progress_values.size() == 1)
        {
            all_ok = expect(listener.progress_values[0] == 50,
                            "host-discovery stats output should map completed hosts to progress") &&
                     all_ok;
        }
    }

    {
        ProgressCapturingListener listener;
        NmapOutputWatch watch;
        watch.progress_mode = NmapProgressMode::host_discovery_stats;
        watch.estimated_total_hosts = 0;
        watch.progress_callback = [&listener](int percent) { listener.on_nmap_progress(percent); };

        const std::string stats_line =
            "Stats: 0:00:05 elapsed; 2 hosts completed (0 up), 1 undergoing Ping Scan\n";
        consume_nmap_output_chunk(watch, stats_line.c_str(), stats_line.size());
        finish_nmap_output_watch(watch);

        all_ok = expect(listener.progress_values.empty(),
                        "host-discovery stats parser should ignore lines when total host count is unknown") &&
                 all_ok;
    }

    {
        ProgressCapturingListener listener;
        NmapOutputWatch watch;
        watch.progress_mode = NmapProgressMode::host_discovery_stats;
        watch.estimated_total_hosts = 8;
        watch.progress_callback = [&listener](int percent) { listener.on_nmap_progress(percent); };

        const std::string progress_line =
            "Ping Scan Timing: About 42.50% done; ETC: 12:00 (0:00:07 remaining)\n";
        consume_nmap_output_chunk(watch, progress_line.c_str(), progress_line.size());
        finish_nmap_output_watch(watch);

        all_ok = expect(listener.progress_values.size() == 1,
                        "host-discovery timing fallback should emit one parsed progress callback") &&
                 all_ok;
        if (listener.progress_values.size() == 1)
        {
            all_ok = expect(listener.progress_values[0] == 42,
                            "host-discovery timing fallback should floor the reported percentage") &&
                     all_ok;
        }
    }

    {
        ProgressCapturingListener listener;
        NmapOutputWatch watch;
        watch.progress_mode = NmapProgressMode::timing_text;
        watch.progress_callback = [&listener](int percent) { listener.on_nmap_progress(percent); };

        const std::string progress_line =
            "Ping Scan Timing: About 17.80% done; ETC: 12:00 (0:00:07 remaining)\r";
        consume_nmap_output_chunk(watch, progress_line.c_str(), progress_line.size());

        all_ok = expect(listener.progress_values.size() == 1,
                        "carriage-return progress output should emit one parsed progress callback") &&
                 all_ok;
        if (listener.progress_values.size() == 1)
        {
            all_ok = expect(listener.progress_values[0] == 17,
                            "carriage-return progress output should be parsed correctly") &&
                     all_ok;
        }
    }

    {
        ProgressCapturingListener listener;
        NmapOutputWatch watch;
        watch.progress_mode = NmapProgressMode::timing_text;
        watch.progress_callback = [&listener](int percent) { listener.on_nmap_progress(percent); };

        const std::string chunk_a = "Ping Scan Timing: About 64.20% done";
        const std::string chunk_b = "; ETC: 12:00 (0:00:07 remaining)\rStats line follows";
        consume_nmap_output_chunk(watch, chunk_a.c_str(), chunk_a.size());
        all_ok = expect(listener.progress_values.empty(),
                        "carriage-return progress parsing should wait for the full line across chunks") &&
                 all_ok;
        consume_nmap_output_chunk(watch, chunk_b.c_str(), chunk_b.size());

        all_ok = expect(listener.progress_values.size() == 1,
                        "split carriage-return progress output should emit one callback") &&
                 all_ok;
        if (listener.progress_values.size() == 1)
        {
            all_ok = expect(listener.progress_values[0] == 64,
                            "split carriage-return progress output should be parsed correctly") &&
                     all_ok;
        }
    }

    {
        ProgressCapturingListener listener;
        NmapOutputWatch watch;
        watch.progress_mode = NmapProgressMode::timing_text;
        watch.progress_callback = [&listener](int percent) { listener.on_nmap_progress(percent); };

        const std::string chunk_a = "Ping Scan Timing: About 100.00";
        const std::string chunk_b = "% done; ETC: 12:00 (0:00:00 remaining)\n";
        consume_nmap_output_chunk(watch, chunk_a.c_str(), chunk_a.size());
        all_ok = expect(listener.progress_values.empty(),
                        "progress parsing should wait for a complete line across chunks") &&
                 all_ok;
        consume_nmap_output_chunk(watch, chunk_b.c_str(), chunk_b.size());
        finish_nmap_output_watch(watch);

        all_ok = expect(listener.progress_values.size() == 1,
                        "split progress output should still emit one callback") &&
                 all_ok;
        if (listener.progress_values.size() == 1)
        {
            all_ok = expect(listener.progress_values[0] == 99,
                            "progress output should cap in-flight progress below completion") &&
                     all_ok;
        }
    }

    {
        ProgressCapturingListener listener;
        NmapOutputWatch watch;
        watch.progress_mode = NmapProgressMode::host_count_xml;
        watch.estimated_total_hosts = 4;
        watch.progress_callback = [&listener](int percent) { listener.on_nmap_progress(percent); };

        const std::string progress_line =
            "SYN Stealth Scan Timing: About 42.50% done; ETC: 12:00 (0:00:07 remaining)\n";
        consume_nmap_output_chunk(watch, progress_line.c_str(), progress_line.size());
        finish_nmap_output_watch(watch);

        all_ok = expect(listener.progress_values.size() == 1,
                        "host-count progress mode should emit timing-text progress callbacks") &&
                 all_ok;
        if (listener.progress_values.size() == 1)
        {
            all_ok = expect(listener.progress_values[0] == 42,
                            "host-count timing output should floor the reported percentage") &&
                     all_ok;
        }
    }

    {
        ProgressCapturingListener listener;
        NmapOutputWatch watch;
        watch.progress_mode = NmapProgressMode::host_discovery_stats;
        watch.estimated_total_hosts = 4;
        watch.progress_callback = [&listener](int percent) { listener.on_nmap_progress(percent); };

        const std::string xml_output =
            "<host starttime=\"1\" endtime=\"2\"><status state=\"up\"/></host>\n";
        consume_nmap_output_chunk(watch, xml_output.c_str(), xml_output.size());
        finish_nmap_output_watch(watch);

        all_ok = expect(listener.progress_values.empty(),
                        "host-discovery stats mode should ignore xml host-count progress lines") &&
                 all_ok;
    }

    {
        ProgressCapturingListener listener;
        NmapOutputWatch watch;
        watch.progress_mode = NmapProgressMode::timing_text;
        watch.progress_callback = [&listener](int percent) { listener.on_nmap_progress(percent); };

        const std::string line_30 =
            "SYN Stealth Scan Timing: About 30.00% done; ETC: 12:00 (0:00:20 remaining)\n";
        const std::string line_07 =
            "Service Scan Timing: About 7.00% done; ETC: 12:00 (0:00:40 remaining)\n";
        const std::string line_08 =
            "Service Scan Timing: About 8.00% done; ETC: 12:00 (0:00:39 remaining)\n";
        consume_nmap_output_chunk(watch, line_30.c_str(), line_30.size());
        consume_nmap_output_chunk(watch, line_07.c_str(), line_07.size());
        consume_nmap_output_chunk(watch, line_08.c_str(), line_08.size());
        finish_nmap_output_watch(watch);

        all_ok = expect(listener.progress_values.size() == 1,
                        "progress must be monotone: lower values after a peak should be suppressed") &&
                 all_ok;
        if (listener.progress_values.size() == 1)
        {
            all_ok = expect(listener.progress_values[0] == 30,
                            "monotone progress should keep the highest reported value") &&
                     all_ok;
        }
    }

    // LSM-004: ETA parsing from timing lines
    {
        std::vector<int> eta_values;
        NmapOutputWatch watch;
        watch.progress_mode = NmapProgressMode::timing_text;
        watch.progress_callback = [](int) {};
        watch.eta_callback = [&eta_values](int s) { eta_values.push_back(s); };

        const std::string line =
            "SYN Stealth Scan Timing: About 42.50% done; ETC: 12:00 (0:01:23 remaining)\n";
        consume_nmap_output_chunk(watch, line.c_str(), line.size());
        finish_nmap_output_watch(watch);

        all_ok = expect(eta_values.size() == 1,
                        "timing line should emit one eta callback") && all_ok;
        if (eta_values.size() == 1)
        {
            all_ok = expect(eta_values[0] == 83,
                            "eta callback should report 83 seconds for (0:01:23 remaining)") &&
                     all_ok;
        }
    }

    {
        std::vector<int> eta_values;
        NmapOutputWatch watch;
        watch.progress_mode = NmapProgressMode::timing_text;
        watch.progress_callback = [](int) {};
        watch.eta_callback = [&eta_values](int s) { eta_values.push_back(s); };

        // Same ETA twice — second should be suppressed
        const std::string line =
            "SYN Stealth Scan Timing: About 42.50% done; ETC: 12:00 (0:01:23 remaining)\n";
        consume_nmap_output_chunk(watch, line.c_str(), line.size());
        consume_nmap_output_chunk(watch, line.c_str(), line.size());
        finish_nmap_output_watch(watch);

        all_ok = expect(eta_values.size() == 1,
                        "duplicate eta value should be suppressed by last_reported_eta guard") &&
                 all_ok;
    }

    {
        std::vector<int> eta_values;
        NmapOutputWatch watch;
        watch.progress_mode = NmapProgressMode::host_count_xml;
        watch.estimated_total_hosts = 4;
        watch.progress_callback = [](int) {};
        watch.eta_callback = [&eta_values](int s) { eta_values.push_back(s); };

        const std::string line =
            "SYN Stealth Scan Timing: About 42.50% done; ETC: 12:00 (0:00:07 remaining)\n";
        consume_nmap_output_chunk(watch, line.c_str(), line.size());
        finish_nmap_output_watch(watch);

        all_ok = expect(eta_values.size() == 1,
                        "host_count_xml mode should also emit eta callback") && all_ok;
        if (eta_values.size() == 1)
        {
            all_ok = expect(eta_values[0] == 7,
                            "eta callback should report 7 seconds for (0:00:07 remaining)") &&
                     all_ok;
        }
    }

    // LSM-005: hosts_found callback in host_count_xml mode
    {
        std::vector<int> hosts_values;
        NmapOutputWatch watch;
        watch.progress_mode = NmapProgressMode::host_count_xml;
        watch.estimated_total_hosts = 10;
        watch.progress_callback = [](int) {};
        watch.hosts_callback = [&hosts_values](int n) { hosts_values.push_back(n); };

        const std::string xml_output =
            "<host starttime=\"1\" endtime=\"2\"><status state=\"up\"/></host>\n"
            "<host starttime=\"2\" endtime=\"3\"><status state=\"up\"/></host>\n"
            "<host starttime=\"3\" endtime=\"4\"><status state=\"up\"/></host>\n";
        consume_nmap_output_chunk(watch, xml_output.c_str(), xml_output.size());
        finish_nmap_output_watch(watch);

        all_ok = expect(hosts_values.size() == 3,
                        "host_count_xml mode should emit incremental hosts_found callbacks") &&
                 all_ok;
        if (hosts_values.size() == 3)
        {
            all_ok = expect(hosts_values[0] == 1, "first host should report count 1") && all_ok;
            all_ok = expect(hosts_values[1] == 2, "second host should report count 2") && all_ok;
            all_ok = expect(hosts_values[2] == 3, "third host should report count 3") && all_ok;
        }
    }

    {
        std::vector<int> hosts_values;
        NmapOutputWatch watch;
        watch.progress_mode = NmapProgressMode::host_count_xml;
        watch.estimated_total_hosts = 4;
        watch.progress_callback = [](int) {};
        watch.hosts_callback = [&hosts_values](int n) { hosts_values.push_back(n); };

        // Same line twice should not emit twice (last_reported_hosts guard)
        const std::string line =
            "<host starttime=\"1\" endtime=\"2\"><status state=\"up\"/></host>\n";
        consume_nmap_output_chunk(watch, line.c_str(), line.size());
        // completed_hosts is now 1; manually verify no duplicate emission if same count sent again
        // (different lines increment completed_hosts so this tests unique values)
        all_ok = expect(hosts_values.size() == 1,
                        "one host element should yield exactly one hosts_found callback") && all_ok;
    }

    {
        // host_discovery_stats mode should NOT emit hosts_found (only host_count_xml does)
        std::vector<int> hosts_values;
        NmapOutputWatch watch;
        watch.progress_mode = NmapProgressMode::host_discovery_stats;
        watch.estimated_total_hosts = 4;
        watch.progress_callback = [](int) {};
        watch.hosts_callback = [&hosts_values](int n) { hosts_values.push_back(n); };

        const std::string xml_output =
            "<host starttime=\"1\" endtime=\"2\"><status state=\"up\"/></host>\n";
        consume_nmap_output_chunk(watch, xml_output.c_str(), xml_output.size());
        finish_nmap_output_watch(watch);

        all_ok = expect(hosts_values.empty(),
                        "host_discovery_stats mode should not emit hosts_found callbacks") &&
                 all_ok;
    }

    {
        all_ok = expect(classify_nmap_progress_mode(make_target_process("-sn", "127.0.0.1")) ==
                            NmapProgressMode::host_discovery_stats,
                        "-sn scans should select host-discovery stats progress mode") &&
                 all_ok;
        all_ok = expect(classify_nmap_progress_mode(make_target_process("-Pn", "127.0.0.1")) ==
                            NmapProgressMode::host_count_xml,
                        "-Pn scans should select xml host-count progress mode") &&
                 all_ok;
        all_ok = expect(classify_nmap_progress_mode(make_target_process("-sV", "127.0.0.1")) ==
                            NmapProgressMode::legacy_mixed,
                        "non-ISS-015 scan shapes should preserve legacy mixed progress mode") &&
                 all_ok;
    }

    {
        const NmapRunResult result = run_nmap_process(make_shell_process(fatal_shell_command()));
        all_ok = expect(!result.ok, "runner should fail when fatal output is seen") && all_ok;
        all_ok = expect(result.error == "invalid target or no hosts resolved",
                        "runner should surface the fatal output error") && all_ok;
        all_ok = expect(result.output.find("Failed to resolve") != std::string::npos,
                        "runner should retain full fatal output") && all_ok;
    }

    {
        const NmapRunResult result = run_nmap_process(make_shell_process(success_shell_command()));
        all_ok = expect(result.ok, "runner should succeed for non-fatal output") && all_ok;
        all_ok = expect(result.exit_code == 0, "runner should keep the process exit code") &&
                 all_ok;
    }

#ifndef _WIN32
    {
        const std::string xml_path = make_temp_path(".xml");
        const std::string marker_path = make_temp_path(".txt");
        ProcessSpec process;
        process.executable = "sh";
        process.args.push_back("-c");
        process.args.push_back("printf '<nmaprun></nmaprun>\\n' > \"$2\"; "
                               "if [ -t 1 ]; then printf tty > \"$3\"; "
                               "else printf pipe > \"$3\"; fi");
        process.args.push_back("runner-test");
        process.args.push_back("-oX");
        process.args.push_back(xml_path);
        process.args.push_back(marker_path);

        const NmapRunResult result = run_nmap_process(process);
        all_ok = expect(result.ok, "runner should succeed for xml file output") && all_ok;
        all_ok = expect(result.output.find("<nmaprun>") != std::string::npos,
                        "runner should return xml file output") && all_ok;
        all_ok = expect(read_file(marker_path) == "pipe",
                        "xml file output should run nmap with pipe stdout, not a pty") &&
                 all_ok;
        unlink(marker_path.c_str());
    }
#endif

    {
        CapturingListener listener;
        NmapRunResult result;
        std::thread runner([&result, &listener]()
                           { result = run_nmap_process(make_shell_process(long_running_shell_command()),
                                                       nullptr, &listener); });

        all_ok = expect(listener.wait_for_start(5000),
                        "runner should publish the process handle for long-running scans") &&
                 all_ok;

        std::string terminate_error;
        const bool terminated = terminate_nmap_process(listener.current_handle(), &terminate_error);
        all_ok = expect(terminated,
                        "terminate_nmap_process should stop the running nmap process") &&
                 all_ok;
        all_ok = expect(terminate_error.empty(),
                        "terminate_nmap_process should not report an error for a live process") &&
                 all_ok;
        all_ok = expect(listener.wait_for_finish(5000),
                        "runner should publish process completion after termination") &&
                 all_ok;

        runner.join();
        all_ok = expect(!result.ok, "runner should report failure after forced termination") &&
                 all_ok;
    }

#ifdef __linux__
    {
        auto count_open_fds = []() -> int {
            int count = 0;
            DIR* dir = opendir("/proc/self/fd");
            if (!dir)
                return -1;
            while (readdir(dir))
                ++count;
            closedir(dir);
            return count - 2; // subtract "." and ".."
        };

        const int fds_before = count_open_fds();
        const NmapRunResult result = run_nmap_process(make_shell_process(success_shell_command()));
        const int fds_after = count_open_fds();

        all_ok = expect(result.ok, "fd-leak: runner should succeed") && all_ok;
        all_ok = expect(fds_before >= 0 && fds_after >= 0,
                        "fd-leak: /proc/self/fd should be readable") && all_ok;
        all_ok = expect(fds_after <= fds_before,
                        "fd-leak: no file descriptors should leak after a complete run") && all_ok;
    }
#endif

    return finish_test("nmap_runner_test", all_ok);
}
