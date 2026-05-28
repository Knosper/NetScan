#include "scan/scan_chunk_runner.hpp"

#include "test_output.hpp"
#include "util/logger.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{
class CapturingSink : public LogSink
{
public:
    std::vector<std::string> lines;

protected:
    void write_line(LogLevel, const std::string& line) override
    {
        lines.push_back(line);
    }
};

bool expect(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << "\n";
        return false;
    }
    return true;
}

ScanRequest make_request(const std::string& target)
{
    ScanRequest request;
    request.target = target;
    request.ports = "22,80";
    request.host_discovery_only = false;
    return request;
}

std::vector<ScanRequest> make_requests()
{
    std::vector<ScanRequest> requests;
    requests.push_back(make_request("192.168.1.0/27"));
    requests.push_back(make_request("192.168.1.32/27"));
    requests.push_back(make_request("192.168.1.64/27"));
    requests.push_back(make_request("192.168.1.96/27"));
    requests.push_back(make_request("192.168.1.128/27"));
    requests.push_back(make_request("192.168.1.160/27"));
    return requests;
}

std::string process_target(const ProcessSpec& process)
{
    if (process.args.empty())
        return "";
    return process.args.back();
}

bool bounded_parallelism_is_enforced()
{
    std::atomic<int> active(0);
    std::atomic<int> max_active(0);
    std::mutex mutex;
    std::vector<std::string> targets;

    const ScanChunkProcessRunner runner =
        [&](const ProcessSpec& process)
        {
            const int current = active.fetch_add(1) + 1;
            int observed = max_active.load();
            while (current > observed && !max_active.compare_exchange_weak(observed, current))
            {
            }

            {
                std::lock_guard<std::mutex> guard(mutex);
                targets.push_back(process_target(process));
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            active.fetch_sub(1);
            return NmapRunResult{true, 0, "<nmaprun></nmaprun>", "", ""};
        };

    ScanChunkRunResult result = run_scan_chunks_with_runner(make_requests(), runner);

    bool ok = true;
    ok = expect(result.ok, "chunk run should succeed") && ok;
    ok = expect(result.outcome == ScanOutcome::Completed,
                "successful chunks should produce completed outcome") &&
         ok;
    ok = expect(result.chunks.size() == make_requests().size(),
                "chunk run should return every chunk result") &&
         ok;
    ok = expect(max_active.load() <= static_cast<int>(scan_chunk_parallelism_limit()),
                "chunk run should respect the parallelism limit") &&
         ok;
    ok = expect(max_active.load() > 1, "chunk run should execute chunks concurrently") && ok;
    ok = expect(std::find(targets.begin(), targets.end(), "192.168.1.160/27") != targets.end(),
                "chunk run should execute the last chunk target") &&
         ok;
    return ok;
}

bool chunk_failure_sets_terminal_failure()
{
    const ScanChunkProcessRunner runner =
        [](const ProcessSpec& process)
        {
            if (process_target(process) == "192.168.1.32/27")
                return NmapRunResult{false, -1, "", "", "forced chunk failure"};
            return NmapRunResult{true, 0, "<nmaprun></nmaprun>", "", ""};
        };

    ScanChunkRunResult result = run_scan_chunks_with_runner(make_requests(), runner);

    bool ok = true;
    ok = expect(!result.ok, "failed chunk run should fail") && ok;
    ok = expect(result.outcome == ScanOutcome::Failed,
                "failed chunk should produce failed outcome") &&
         ok;
    ok = expect(result.message == "chunk scan failed", "failed chunk should set clear message") &&
         ok;
    ok = expect(result.stderr_text == "forced chunk failure",
                "failed chunk should expose diagnostic text") &&
         ok;
    return ok;
}

bool chunk_abort_sets_terminal_abort()
{
    const ScanChunkProcessRunner runner =
        [](const ProcessSpec& process)
        {
            if (process_target(process) == "192.168.1.64/27")
                return NmapRunResult{false, -1, "", "", "nmap process terminated by user"};
            return NmapRunResult{true, 0, "<nmaprun></nmaprun>", "", ""};
        };

    ScanChunkRunResult result = run_scan_chunks_with_runner(make_requests(), runner);

    bool ok = true;
    ok = expect(!result.ok, "aborted chunk run should fail") && ok;
    ok = expect(result.outcome == ScanOutcome::Aborted,
                "aborted chunk should produce aborted outcome") &&
         ok;
    ok = expect(result.message == "chunk scan aborted", "aborted chunk should set clear message") &&
         ok;
    return ok;
}

bool chunk_logging_reports_start_finish_and_summary()
{
    Logger logger;
    std::unique_ptr<CapturingSink> sink(new CapturingSink());
    CapturingSink* capture = sink.get();
    logger.add_sink(std::move(sink));

    const ScanChunkProcessRunner runner =
        [](const ProcessSpec&)
        {
            return NmapRunResult{true, 0, "<nmaprun></nmaprun>", "", ""};
        };

    ScanChunkRunResult result = run_scan_chunks_with_runner(make_requests(), runner, &logger);

    bool saw_start = false;
    bool saw_finish = false;
    bool saw_summary = false;
    for (const std::string& line : capture->lines)
    {
        saw_start = saw_start || line.find("scan chunk started: 1/6") != std::string::npos;
        saw_finish = saw_finish || line.find("scan chunk finished: 6/6") != std::string::npos;
        saw_summary = saw_summary || line.find("scan chunk summary: chunks=6") != std::string::npos;
    }

    bool ok = true;
    ok = expect(result.ok, "logged chunk run should succeed") && ok;
    ok = expect(saw_start, "chunk logging should include a start line") && ok;
    ok = expect(saw_finish, "chunk logging should include a finish line") && ok;
    ok = expect(saw_summary, "chunk logging should include a summary line") && ok;
    return ok;
}
} // namespace

int main()
{
    bool all_ok = true;
    all_ok = bounded_parallelism_is_enforced() && all_ok;
    all_ok = chunk_failure_sets_terminal_failure() && all_ok;
    all_ok = chunk_abort_sets_terminal_abort() && all_ok;
    all_ok = chunk_logging_reports_start_finish_and_summary() && all_ok;
    return finish_test("scan_chunk_runner_test", all_ok);
}
