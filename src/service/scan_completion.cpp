#include "service/scan_completion.hpp"
#include "service/scan_result_builders.hpp"

#include "db/sqlite_helpers.hpp"
#include "db/write_transaction.hpp"
#include "scan/nmap_xml_parser.hpp"
#include "scan/port_spec_validator.hpp"
#include "scan/scan_snapshot_merger.hpp"
#include "service/scan_messages.hpp"
#include "util/logger.hpp"

ScanCompletionOrchestrator::ScanCompletionOrchestrator(Database& db,
                                                        ScanRepository& repo,
                                                        Logger& logger)
    : db_(db), repo_(repo), logger_(logger)
{
}

PersistedScanState ScanCompletionOrchestrator::state_for_outcome(ScanOutcome outcome)
{
    switch (outcome)
    {
    case ScanOutcome::Completed:
        return PersistedScanState::Completed;
    case ScanOutcome::Aborted:
        return PersistedScanState::Aborted;
    case ScanOutcome::DependencyMissing:
        return PersistedScanState::DependencyMissing;
    case ScanOutcome::Failed:
    case ScanOutcome::Rejected:
    default:
        return PersistedScanState::Failed;
    }
}

bool ScanCompletionOrchestrator::persist_terminal_state_transaction(
    sqlite3* h, const std::function<bool()>& mark_fn)
{
    try
    {
        WriteTransaction tx(h, &logger_);
        if (!mark_fn())
            return false;
        tx.commit();
        return true;
    }
    catch (const std::exception& e)
    {
        logger_.error(std::string("terminal scan transaction error: ") + e.what());
        return false;
    }
}

bool ScanCompletionOrchestrator::persist_completed_transaction(
    sqlite3* h, const ScanCompletionInput& input, const CompletedScanSnapshot& snapshot)
{
    try
    {
        WriteTransaction tx(h, &logger_);
        if (!persist_completed_scan_snapshot(h, repo_, logger_, snapshot))
            return false;
        if (!repo_.mark_scan_completed(h, input.scan_id, input.result, input.finished_at))
            return false;
        tx.commit();
        return true;
    }
    catch (const std::exception& e)
    {
        logger_.error(std::string("completed scan transaction error: ") + e.what());
        return false;
    }
}

bool ScanCompletionOrchestrator::persist_failed_fallback_transaction(
    sqlite3* h, int scan_id, const std::string& finished_at, const std::string& message)
{
    return persist_terminal_state_transaction(h, [this, h, scan_id, &finished_at, &message]() {
        const ScanResult fallback = failed_scan_result(message);
        return repo_.mark_scan_failed(h, scan_id, fallback, finished_at, true);
    });
}

bool ScanCompletionOrchestrator::persist_terminal(const ScanCompletionInput& input,
                                                  const CompletedScanSnapshot* snapshot)
{
    switch (state_for_outcome(input.result.outcome))
    {
    case PersistedScanState::Aborted:
        return persist_aborted(input);

    case PersistedScanState::Completed:
        if (!snapshot)
        {
            logger_.error("internal error: completed scan without snapshot");
            return false;
        }
        return persist_completed(input, *snapshot);

    case PersistedScanState::DependencyMissing:
        return persist_dependency_missing(input);

    case PersistedScanState::Failed:
    default:
        return persist_failed(input);
    }
}

bool ScanCompletionOrchestrator::persist_simple_terminal_state(
    std::function<bool(sqlite3*)> mark)
{
    return db_.write([this, mark = std::move(mark)](sqlite3* h) {
        return persist_terminal_state_transaction(h, [h, &mark]() { return mark(h); });
    });
}

bool ScanCompletionOrchestrator::persist_aborted(const ScanCompletionInput& input)
{
    return persist_simple_terminal_state([this, &input](sqlite3* h) {
        return repo_.mark_scan_aborted(h, input.scan_id, input.result, input.finished_at);
    });
}

bool ScanCompletionOrchestrator::persist_completed(const ScanCompletionInput& input,
                                                   const CompletedScanSnapshot& snapshot)
{
    return db_.write([this, &input, &snapshot](sqlite3* h) {
        bool ok = persist_completed_transaction(h, input, snapshot);
        if (ok)
            return true;

        ok = persist_failed_fallback_transaction(
            h, input.scan_id, input.finished_at,
            lsm::service::scan_messages::FAILED_TO_PERSIST_COMPLETED_RESULTS);
        if (!ok)
        {
            logger_.error("failed to persist fallback failed state after completed scan "
                          "transaction error");
        }
        return ok;
    });
}

bool ScanCompletionOrchestrator::persist_dependency_missing(const ScanCompletionInput& input)
{
    return persist_simple_terminal_state([this, &input](sqlite3* h) {
        return repo_.mark_scan_dependency_missing(h, input.scan_id, input.result,
                                                  input.finished_at);
    });
}

bool ScanCompletionOrchestrator::persist_failed(const ScanCompletionInput& input)
{
    return persist_simple_terminal_state([this, &input](sqlite3* h) {
        return repo_.mark_scan_failed(h, input.scan_id, input.result,
                                      input.finished_at, true);
    });
}

bool ScanCompletionOrchestrator::parse_completed_snapshot(const ScanCompletionInput& input,
                                                          CompletedScanSnapshot& snapshot)
{
    lsm::scan::NmapXmlParseResult parse_result =
        lsm::scan::parse_nmap_xml(input.result.raw_output);
    if (!parse_result.ok)
    {
        logger_.error("XML parser failed for completed scan: " + parse_result.error);
        return false;
    }

    snapshot.scan_id = input.scan_id;
    snapshot.host_discovery_only = input.request.host_discovery_only;
    snapshot.port_coverage = derive_scan_port_coverage(input.request);
    snapshot.snapshot = std::move(parse_result.snapshot);
    return true;
}

bool ScanCompletionOrchestrator::parse_chunked_completed_snapshot(
    const ScanCompletionInput& input, const ScanChunkRunResult& chunk_result,
    CompletedScanSnapshot& snapshot)
{
    std::vector<std::string> xml_outputs;
    xml_outputs.reserve(chunk_result.chunks.size());
    for (const ScanChunkExecutionResult& chunk : chunk_result.chunks)
        xml_outputs.push_back(chunk.run_result.output);

    lsm::scan::ScanSnapshotMergeResult merge_result =
        lsm::scan::merge_nmap_xml_snapshots(xml_outputs);
    if (!merge_result.ok)
    {
        logger_.error("XML parser failed for chunked completed scan: " + merge_result.error);
        return false;
    }

    snapshot.scan_id = input.scan_id;
    snapshot.host_discovery_only = input.request.host_discovery_only;
    snapshot.port_coverage = derive_scan_port_coverage(input.request);
    snapshot.snapshot = std::move(merge_result.snapshot);
    return true;
}

ScanCompletionResult ScanCompletionOrchestrator::make_parse_failure_result(
    const ScanCompletionInput& input)
{
    ScanCompletionResult result;
    result.final_state = PersistedScanState::Failed;
    result.final_message = lsm::service::scan_messages::FAILED_TO_PARSE_RESULTS;

    ScanCompletionInput fallback = input;
    fallback.result.outcome = ScanOutcome::Failed;
    fallback.result.message = result.final_message;
    result.ok = persist_terminal(fallback, nullptr);
    return result;
}

ScanCompletionResult ScanCompletionOrchestrator::complete(const ScanCompletionInput& input)
{
    ScanCompletionResult result;
    result.final_state = state_for_outcome(input.result.outcome);
    result.final_message = input.result.message;

    CompletedScanSnapshot snapshot;
    const bool should_parse = result.final_state == PersistedScanState::Completed;
    if (should_parse && !parse_completed_snapshot(input, snapshot))
        return make_parse_failure_result(input);

    result.ok = persist_terminal(input, should_parse ? &snapshot : nullptr);
    return result;
}

ScanCompletionResult ScanCompletionOrchestrator::complete_chunked(
    const ScanCompletionInput& input, const ScanChunkRunResult& chunk_result)
{
    ScanCompletionInput chunked_input = input;
    chunked_input.result.outcome = chunk_result.outcome;
    chunked_input.result.message = chunk_result.message;
    chunked_input.result.stderr_text = chunk_result.stderr_text;

    ScanCompletionResult result;
    result.final_state = state_for_outcome(chunked_input.result.outcome);
    result.final_message = chunked_input.result.message;

    CompletedScanSnapshot snapshot;
    const bool should_parse = result.final_state == PersistedScanState::Completed;
    if (should_parse && !parse_chunked_completed_snapshot(chunked_input, chunk_result, snapshot))
        return make_parse_failure_result(chunked_input);

    result.ok = persist_terminal(chunked_input, should_parse ? &snapshot : nullptr);
    return result;
}
