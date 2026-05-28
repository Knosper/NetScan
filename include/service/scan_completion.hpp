#ifndef SERVICE_SCAN_COMPLETION_HPP
#define SERVICE_SCAN_COMPLETION_HPP

#include "db/database.hpp"
#include "db/scan_repository.hpp"
#include "scan/scan_snapshot.hpp"
#include "scan/scan_chunk_runner.hpp"
#include "scan/scan_types.hpp"
#include "service/scan_persistence.hpp"
#include "util/logger.hpp"

#include <functional>
#include <memory>
#include <string>

struct ScanCompletionInput
{
    int scan_id = -1;
    ScanRequest request;
    ScanResult result;
    std::string finished_at;
};

struct ScanCompletionResult
{
    bool ok = false;
    PersistedScanState final_state = PersistedScanState::Failed;
    std::string final_message;
};

class ScanCompletionOrchestrator
{
public:
    ScanCompletionOrchestrator(Database& db, ScanRepository& repo, Logger& logger);

    ScanCompletionResult complete(const ScanCompletionInput& input);
    ScanCompletionResult complete_chunked(const ScanCompletionInput& input,
                                          const ScanChunkRunResult& chunk_result);

private:
    Database& db_;
    ScanRepository& repo_;
    Logger& logger_;

    bool persist_terminal_state_transaction(sqlite3* h,
                                             const std::function<bool()>& mark_fn);
    bool persist_completed_transaction(sqlite3* h,
                                        const ScanCompletionInput& input,
                                        const CompletedScanSnapshot& snapshot);
    bool persist_failed_fallback_transaction(sqlite3* h, int scan_id,
                                             const std::string& finished_at,
                                             const std::string& message);
    bool persist_terminal(const ScanCompletionInput& input,
                          const CompletedScanSnapshot* snapshot);
    bool persist_simple_terminal_state(std::function<bool(sqlite3*)> mark);
    bool persist_aborted(const ScanCompletionInput& input);
    bool persist_completed(const ScanCompletionInput& input,
                           const CompletedScanSnapshot& snapshot);
    bool persist_dependency_missing(const ScanCompletionInput& input);
    bool persist_failed(const ScanCompletionInput& input);
    bool parse_completed_snapshot(const ScanCompletionInput& input,
                                  CompletedScanSnapshot& snapshot);
    bool parse_chunked_completed_snapshot(const ScanCompletionInput& input,
                                          const ScanChunkRunResult& chunk_result,
                                          CompletedScanSnapshot& snapshot);
    ScanCompletionResult make_parse_failure_result(const ScanCompletionInput& input);
    static PersistedScanState state_for_outcome(ScanOutcome outcome);
};

#endif
