#include "db/database.hpp"
#include "db/schema.hpp"
#include "db/scan_repository.hpp"
#include "db/sqlite_helpers.hpp"
#include "service/scan_completion.hpp"
#include "service/scan_messages.hpp"
#include "scan_service_test_support.hpp"
#include "test_output.hpp"
#include "util/logger.hpp"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace
{
const char FINISHED_AT[] = "2026-01-01 00:00:02";

int insert_running_scan(Database& db, ScanRepository& repo, const ScanRequest& request)
{
    return db.write(
        [&](sqlite3* h)
        {
            const int scan_id = repo.insert_scan_queued(h, request);
            if (scan_id <= 0)
                return -1;
            if (!repo.mark_scan_running(h, scan_id, "2026-01-01 00:00:01"))
                return -1;
            return scan_id;
        });
}

ScanCompletionInput make_completion_input(int scan_id, const ScanRequest& request,
                                          const std::string& raw_output)
{
    ScanCompletionInput input;
    input.scan_id = scan_id;
    input.request = request;
    input.finished_at = FINISHED_AT;
    input.result.outcome = ScanOutcome::Completed;
    input.result.message = lsm::service::scan_messages::SCAN_COMPLETED;
    input.result.command = "nmap -- test";
    input.result.exit_code = 0;
    input.result.raw_output = raw_output;
    return input;
}

std::string single_host_xml()
{
    return R"XML(
<?xml version="1.0"?>
<nmaprun>
<host>
    <status state="up"/>
    <address addr="10.0.0.10" addrtype="ipv4"/>
    <ports>
        <port protocol="tcp" portid="22">
            <state state="open"/>
            <service name="ssh"/>
        </port>
    </ports>
</host>
</nmaprun>
)XML";
}

std::string chunk_one_xml()
{
    return R"XML(
<?xml version="1.0"?>
<nmaprun>
<host>
    <status state="up"/>
    <address addr="10.0.1.10" addrtype="ipv4"/>
    <hostnames>
        <hostname name="alpha.local"/>
    </hostnames>
    <ports>
        <port protocol="tcp" portid="22">
            <state state="open"/>
            <service name="ssh"/>
        </port>
    </ports>
</host>
<host>
    <status state="up"/>
    <address addr="10.0.1.11" addrtype="ipv4"/>
</host>
</nmaprun>
)XML";
}

std::string chunk_two_xml()
{
    return R"XML(
<?xml version="1.0"?>
<nmaprun>
<host>
    <status state="up"/>
    <address addr="10.0.1.10" addrtype="ipv4"/>
    <ports>
        <port protocol="tcp" portid="22">
            <state state="open"/>
            <service name="ssh"/>
        </port>
        <port protocol="tcp" portid="80">
            <state state="open"/>
            <service name="http"/>
        </port>
    </ports>
</host>
<host>
    <status state="up"/>
    <address addr="10.0.1.12" addrtype="ipv4"/>
    <ports>
        <port protocol="tcp" portid="443">
            <state state="open"/>
            <service name="https"/>
        </port>
    </ports>
</host>
</nmaprun>
)XML";
}

ScanChunkRunResult make_completed_chunk_result()
{
    ScanChunkRunResult result;
    result.ok = true;
    result.outcome = ScanOutcome::Completed;
    result.message = lsm::service::scan_messages::SCAN_COMPLETED;

    ScanChunkExecutionResult first;
    first.request = {"10.0.1.0/27", "22,80,443", false, ""};
    first.run_result = {true, 0, chunk_one_xml(), "", ""};
    result.chunks.push_back(first);

    ScanChunkExecutionResult second;
    second.request = {"10.0.1.32/27", "22,80,443", false, ""};
    second.run_result = {true, 0, chunk_two_xml(), "", ""};
    result.chunks.push_back(second);
    return result;
}

bool expect_scan_state(sqlite3* h, ScanRepository& repo, int scan_id, PersistedScanState state,
                       const std::string& message)
{
    std::unique_ptr<PersistedScanSummary> scan = repo.get_scan_by_id(h, scan_id);
    if (!expect(scan != nullptr, "scan should be readable after completion"))
        return false;

    bool ok = true;
    ok = expect(scan->state == state, "scan should have expected final state") && ok;
    ok = expect(scan->message == message, "scan should have expected final message") && ok;
    ok = expect(scan->finished_at == FINISHED_AT, "scan should persist finished_at") && ok;
    return ok;
}

bool chunked_scan_persists_merged_snapshot(Database& db, ScanRepository& repo, Logger& logger)
{
    const ScanRequest request{"10.0.1.0/26", "22,80,443", false, ""};
    const int scan_id = insert_running_scan(db, repo, request);
    bool ok = expect(scan_id > 0, "chunked running scan fixture should be inserted") && true;
    if (scan_id <= 0)
        return ok;

    ScanCompletionOrchestrator orchestrator(db, repo, logger);
    ScanCompletionInput input = make_completion_input(scan_id, request, "");
    const ScanCompletionResult result =
        orchestrator.complete_chunked(input, make_completed_chunk_result());

    ok = expect(result.ok, "chunked scan completion should succeed") && ok;
    ok = expect(result.final_state == PersistedScanState::Completed,
                "chunked scan should return completed state") &&
         ok;
    db.read([&](sqlite3* h) {
        const std::vector<ScanHostRow> hosts = repo.list_scan_hosts(h, scan_id);
        const std::vector<ScanPortRow> ports = repo.list_scan_ports(h, scan_id);
        const std::vector<ScanPortCoverageRow> coverage =
            repo.list_scan_port_coverage(h, scan_id);
        ok = expect_scan_state(h, repo, scan_id, PersistedScanState::Completed,
                               lsm::service::scan_messages::SCAN_COMPLETED) &&
             ok;
        ok = expect(hosts.size() == 3, "merged chunk snapshot should persist three hosts") && ok;
        ok = expect(ports.size() == 3, "merged chunk snapshot should persist three ports") && ok;
        ok = expect(coverage.size() == 3,
                    "chunked completion should persist normalized requested coverage once") &&
             ok;
        ok = expect(hosts[1].ip == "10.0.1.11",
                    "host without open ports should remain in merged snapshot") &&
             ok;
    });
    return ok;
}

bool completed_scan_persists_snapshot(Database& db, ScanRepository& repo, Logger& logger)
{
    const ScanRequest request{"10.0.0.10", "22", false, ""};
    const int scan_id = insert_running_scan(db, repo, request);
    bool ok = expect(scan_id > 0, "running scan fixture should be inserted") && true;
    if (scan_id <= 0)
        return ok;

    ScanCompletionOrchestrator orchestrator(db, repo, logger);
    const ScanCompletionResult result =
        orchestrator.complete(make_completion_input(scan_id, request, single_host_xml()));

    ok = expect(result.ok, "completed scan completion should succeed") && ok;
    ok = expect(result.final_state == PersistedScanState::Completed,
                "completed scan should return completed state") && ok;
    db.read([&](sqlite3* h) {
        ok = expect_scan_state(h, repo, scan_id, PersistedScanState::Completed,
                               lsm::service::scan_messages::SCAN_COMPLETED) &&
             ok;
        ok = expect(repo.list_scan_hosts(h, scan_id).size() == 1,
                    "completed scan should persist host snapshot") &&
             ok;
        ok = expect(repo.list_scan_ports(h, scan_id).size() == 1,
                    "completed scan should persist port snapshot") &&
             ok;
        ok = expect(repo.scan_has_known_port_coverage(h, scan_id),
                    "completed scan should persist known port coverage") &&
             ok;
        ok = expect(repo.scan_covers_port(h, scan_id, 22),
                    "completed scan should persist targeted port coverage") &&
             ok;
    });
    return ok;
}

bool parser_failure_marks_failed_without_snapshot(Database& db, ScanRepository& repo,
                                                  Logger& logger)
{
    const ScanRequest request{"10.0.0.11", "22", false, ""};
    const int scan_id = insert_running_scan(db, repo, request);
    bool ok = expect(scan_id > 0, "parser failure fixture should be inserted") && true;
    if (scan_id <= 0)
        return ok;

    ScanCompletionOrchestrator orchestrator(db, repo, logger);
    ScanCompletionResult result =
        orchestrator.complete(make_completion_input(scan_id, request, "<not-xml>"));

    ok = expect(result.ok, "parser failure should persist failed terminal state") && ok;
    ok = expect(result.final_state == PersistedScanState::Failed,
                "parser failure should return failed state") &&
         ok;
    ok = expect(result.final_message == lsm::service::scan_messages::FAILED_TO_PARSE_RESULTS,
                "parser failure should use central message") &&
         ok;
    db.read([&](sqlite3* h) {
        ok = expect_scan_state(h, repo, scan_id, PersistedScanState::Failed,
                               lsm::service::scan_messages::FAILED_TO_PARSE_RESULTS) &&
             ok;
        ok = expect(repo.list_scan_hosts(h, scan_id).empty(),
                    "parser failure should not persist snapshot hosts") &&
             ok;
    });
    return ok;
}

bool aborted_scan_persists_terminal_only(Database& db, ScanRepository& repo, Logger& logger)
{
    const ScanRequest request{"10.0.0.12", "22", false, ""};
    const int scan_id = insert_running_scan(db, repo, request);
    bool ok = expect(scan_id > 0, "abort fixture should be inserted") && true;
    if (scan_id <= 0)
        return ok;

    ScanCompletionInput input = make_completion_input(scan_id, request, single_host_xml());
    input.result.outcome = ScanOutcome::Aborted;
    input.result.message = lsm::service::scan_messages::SCAN_ABORTED_BY_USER;

    ScanCompletionOrchestrator orchestrator(db, repo, logger);
    ScanCompletionResult result = orchestrator.complete(input);

    ok = expect(result.ok, "aborted scan completion should succeed") && ok;
    ok = expect(result.final_state == PersistedScanState::Aborted,
                "aborted scan should return aborted state") &&
         ok;
    db.read([&](sqlite3* h) {
        ok = expect_scan_state(h, repo, scan_id, PersistedScanState::Aborted,
                               lsm::service::scan_messages::SCAN_ABORTED_BY_USER) &&
             ok;
        ok = expect(repo.list_scan_hosts(h, scan_id).empty(),
                    "aborted scan should not persist snapshot hosts") &&
             ok;
    });
    return ok;
}

bool snapshot_failure_rolls_back_and_falls_back(Database& db, ScanRepository& repo,
                                                Logger& logger)
{
    const ScanRequest request{"10.0.0.20", "22", false, ""};
    const int scan_id = insert_running_scan(db, repo, request);
    bool ok = expect(scan_id > 0, "persistence failure fixture should be inserted") && true;
    if (scan_id <= 0)
        return ok;

    bool trigger_created = false;
    db.write([&](sqlite3* h) {
        std::string error;
        trigger_created = db_exec(
            h,
            "CREATE TRIGGER fail_scan_completion_snapshot "
            "BEFORE INSERT ON scan_hosts "
            "BEGIN SELECT RAISE(FAIL, 'forced scan_hosts failure'); END;",
            &error);
    });
    ok = expect(trigger_created, "snapshot failure trigger should be installed") && ok;
    if (!trigger_created)
        return ok;

    ScanCompletionOrchestrator orchestrator(db, repo, logger);
    ScanCompletionResult result =
        orchestrator.complete(make_completion_input(scan_id, request, single_host_xml()));

    ok = expect(result.ok, "fallback failed state should persist after snapshot failure") && ok;
    db.read([&](sqlite3* h) {
        ok = expect_scan_state(
                 h, repo, scan_id, PersistedScanState::Failed,
                 lsm::service::scan_messages::FAILED_TO_PERSIST_COMPLETED_RESULTS) &&
             ok;
        ok = expect(repo.list_scan_hosts(h, scan_id).empty(),
                    "rolled back snapshot failure should not leave scan hosts") &&
             ok;
        ok = expect(repo.list_scan_ports(h, scan_id).empty(),
                    "rolled back snapshot failure should not leave scan ports") &&
             ok;
    });
    db.write([&](sqlite3* h) {
        std::string error;
        ok = expect(db_exec(h, "DROP TRIGGER fail_scan_completion_snapshot;", &error),
                    "snapshot failure trigger should be removed") &&
             ok;
    });
    return ok;
}

bool stale_second_completion_cannot_overwrite_terminal_state(Database& db,
                                                             ScanRepository& repo,
                                                             Logger& logger)
{
    const ScanRequest request{"10.0.0.30", "22", false, ""};
    const int scan_id = insert_running_scan(db, repo, request);
    bool ok = expect(scan_id > 0, "stale completion fixture should be inserted") && true;
    if (scan_id <= 0)
        return ok;

    ScanCompletionOrchestrator orchestrator(db, repo, logger);
    ScanCompletionResult first =
        orchestrator.complete(make_completion_input(scan_id, request, single_host_xml()));
    ok = expect(first.ok, "first completion should persist") && ok;

    ScanCompletionInput stale = make_completion_input(scan_id, request, single_host_xml());
    stale.result.outcome = ScanOutcome::Aborted;
    stale.result.message = lsm::service::scan_messages::SCAN_ABORTED_BY_USER;
    stale.finished_at = "2026-01-01 00:00:03";

    ScanCompletionResult second = orchestrator.complete(stale);
    ok = expect(!second.ok, "stale second completion should be rejected") && ok;

    db.read([&](sqlite3* h) {
        ok = expect_scan_state(h, repo, scan_id, PersistedScanState::Completed,
                               lsm::service::scan_messages::SCAN_COMPLETED) &&
             ok;
        ok = expect(repo.list_scan_hosts(h, scan_id).size() == 1,
                    "stale completion should not roll back or replace first snapshot") &&
             ok;
    });
    return ok;
}
} // namespace

int main()
{
    bool all_ok = true;
    const std::string db_path = make_temp_db_path("/tmp/netscan-scan-completion-test-XXXXXX.db");
    all_ok = expect(!db_path.empty(), "temporary database path should be created") && all_ok;
    if (!all_ok)
        return finish_test("scan_completion_test", false);

    {
        Database db(db_path);
        Logger logger;
        configure_database_runtime(db, logger);
        init_schema(db, logger);
        ScanRepository repo(db);

        all_ok = completed_scan_persists_snapshot(db, repo, logger) && all_ok;
        all_ok = chunked_scan_persists_merged_snapshot(db, repo, logger) && all_ok;
        all_ok = parser_failure_marks_failed_without_snapshot(db, repo, logger) && all_ok;
        all_ok = aborted_scan_persists_terminal_only(db, repo, logger) && all_ok;
        all_ok = snapshot_failure_rolls_back_and_falls_back(db, repo, logger) && all_ok;
        all_ok = stale_second_completion_cannot_overwrite_terminal_state(db, repo, logger) &&
                 all_ok;
    }

    std::remove(db_path.c_str());
    return finish_test("scan_completion_test", all_ok);
}
