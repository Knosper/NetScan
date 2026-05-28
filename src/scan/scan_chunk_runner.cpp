#include "scan/scan_chunk_runner.hpp"

#include "scan/nmap_command_builder.hpp"
#include "scan/process_renderer.hpp"
#include "util/logger.hpp"
#include <algorithm>
#include <atomic>
#include <mutex>
#include <sstream>
#include <thread>

namespace
{
constexpr std::size_t kParallelismLimit = 4;
constexpr char MSG_CHUNK_SCAN_COMPLETED[] = "chunk scan completed";
constexpr char MSG_CHUNK_SCAN_FAILED[] = "chunk scan failed";
constexpr char MSG_CHUNK_SCAN_ABORTED[] = "chunk scan aborted";
constexpr char MSG_CHUNK_SCAN_NON_ZERO_EXIT[] = "chunk nmap exited with non-zero status";

bool contains_text(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

bool is_abort_result(const NmapRunResult& result)
{
    return contains_text(result.error, "aborted") ||
           contains_text(result.error, "terminated") ||
           contains_text(result.error, "killed by signal");
}

std::string diagnostic_text(const NmapRunResult& result)
{
    if (!result.stderr_text.empty())
        return result.stderr_text;
    if (!result.error.empty())
        return result.error;
    return result.output;
}

std::string chunk_label(std::size_t index, std::size_t total, const ScanRequest& request)
{
    return std::to_string(index + 1) + "/" + std::to_string(total) + " target=" + request.target;
}

void log_chunk_started(Logger* logger, std::size_t index, std::size_t total,
                       const ScanRequest& request)
{
    if (!logger)
        return;
    logger->info("scan chunk started: " + chunk_label(index, total, request));
}

void log_chunk_finished(Logger* logger, std::size_t index, std::size_t total,
                        const ScanChunkExecutionResult& result)
{
    if (!logger)
        return;

    std::ostringstream message;
    message << "scan chunk finished: " << chunk_label(index, total, result.request)
            << " ok=" << (result.run_result.ok ? "true" : "false")
            << " exit_code=" << result.run_result.exit_code;
    if (!result.run_result.error.empty())
        message << " error=" << result.run_result.error;
    logger->info(message.str());
}

void log_chunk_summary(Logger* logger, const ScanChunkRunResult& result)
{
    if (!logger)
        return;
    logger->info("scan chunk summary: chunks=" + std::to_string(result.chunks.size()) +
                 " outcome=" + result.message);
}

ScanChunkExecutionResult execute_chunk_request(const ScanRequest& request,
                                               const ScanChunkProcessRunner& runner)
{
    ScanChunkExecutionResult result;
    result.request = request;

    NmapCommandBuildResult build_result = build_nmap_command(request);
    if (!build_result.ok)
    {
        result.run_result = {false, -1, "", "", build_result.error};
        return result;
    }

    result.process = build_result.process;
    result.command = render_process_for_log(result.process);
    result.run_result = runner(result.process);
    return result;
}

void run_chunk_worker(const std::vector<ScanRequest>& requests,
                      const ScanChunkProcessRunner& runner,
                      std::atomic<std::size_t>& next_index,
                      std::vector<ScanChunkExecutionResult>& results,
                      Logger* logger)
{
    const std::size_t total = requests.size();
    for (;;)
    {
        const std::size_t index = next_index.fetch_add(1);
        if (index >= requests.size())
            return;

        log_chunk_started(logger, index, total, requests[index]);
        results[index] = execute_chunk_request(requests[index], runner);
        log_chunk_finished(logger, index, total, results[index]);
    }
}

ScanChunkRunResult summarize_chunk_results(std::vector<ScanChunkExecutionResult>& results)
{
    for (const auto& chunk : results)
    {
        if (is_abort_result(chunk.run_result))
            return {false, ScanOutcome::Aborted, MSG_CHUNK_SCAN_ABORTED, results,
                    diagnostic_text(chunk.run_result)};

        if (!chunk.run_result.ok)
            return {false, ScanOutcome::Failed, MSG_CHUNK_SCAN_FAILED, results,
                    diagnostic_text(chunk.run_result)};

        if (chunk.run_result.exit_code != 0)
            return {false, ScanOutcome::Failed, MSG_CHUNK_SCAN_NON_ZERO_EXIT, results,
                    diagnostic_text(chunk.run_result)};
    }

    return {true, ScanOutcome::Completed, MSG_CHUNK_SCAN_COMPLETED, results, ""};
}

ScanChunkRunResult run_chunk_requests(const std::vector<ScanRequest>& requests,
                                      const ScanChunkProcessRunner& runner,
                                      Logger* logger)
{
    std::vector<ScanChunkExecutionResult> results(requests.size());
    std::atomic<std::size_t> next_index(0);
    const std::size_t worker_count =
        std::min(scan_chunk_parallelism_limit(), requests.size());

    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    for (std::size_t i = 0; i < worker_count; ++i)
        workers.push_back(std::thread(run_chunk_worker, std::cref(requests), std::cref(runner),
                                      std::ref(next_index), std::ref(results), logger));

    for (std::thread& worker : workers)
        worker.join();

    ScanChunkRunResult summary = summarize_chunk_results(results);
    log_chunk_summary(logger, summary);
    return summary;
}
} // namespace

std::size_t scan_chunk_parallelism_limit()
{
    return kParallelismLimit;
}

ScanChunkRunResult run_scan_chunks_with_runner(const std::vector<ScanRequest>& requests,
                                               const ScanChunkProcessRunner& runner)
{
    return run_chunk_requests(requests, runner, nullptr);
}

ScanChunkRunResult run_scan_chunks_with_runner(const std::vector<ScanRequest>& requests,
                                               const ScanChunkProcessRunner& runner,
                                               Logger* logger)
{
    return run_chunk_requests(requests, runner, logger);
}
