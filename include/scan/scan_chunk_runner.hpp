#ifndef SCAN_CHUNK_RUNNER_HPP
#define SCAN_CHUNK_RUNNER_HPP

#include "scan/nmap_runner.hpp"
#include "scan/process_spec.hpp"
#include "scan/scan_types.hpp"
#include <functional>
#include <string>
#include <vector>

struct ScanChunkExecutionResult
{
    ScanRequest request;
    ProcessSpec process;
    std::string command;
    NmapRunResult run_result;
};

struct ScanChunkRunResult
{
    bool ok = false;
    ScanOutcome outcome = ScanOutcome::Failed;
    std::string message;
    std::vector<ScanChunkExecutionResult> chunks;
    std::string stderr_text;
};

typedef std::function<NmapRunResult(const ProcessSpec&)> ScanChunkProcessRunner;

std::size_t scan_chunk_parallelism_limit();
ScanChunkRunResult run_scan_chunks_with_runner(const std::vector<ScanRequest>& requests,
                                               const ScanChunkProcessRunner& runner);
ScanChunkRunResult run_scan_chunks_with_runner(const std::vector<ScanRequest>& requests,
                                               const ScanChunkProcessRunner& runner,
                                               Logger* logger);

#endif
