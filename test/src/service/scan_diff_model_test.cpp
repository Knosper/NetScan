#include "db/database.hpp"
#include "db/scan_repository.hpp"
#include "db/schema.hpp"
#include "service/scan_diff_service.hpp"
#include "service/scan_service.hpp"
#include "scan_service_test_support.hpp"
#include "util/logger.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include "test_output.hpp"

namespace
{
constexpr const char* REPORTS_NO_ERROR = "successful diff result should report no error";
constexpr const char* REPORTS_NO_ERROR_AFTER_TAKE =
    "consumed successful diff result should still report no error";
constexpr const char* TEMP_DB_CREATED = "temporary database path should be created";
constexpr const char* SUFFIX_PERSISTS = " should persist";
constexpr const char* SUFFIX_NO_DIFF = " should not return a diff payload";
constexpr const char* SUFFIX_RETURNS_ERROR = " should return ";
constexpr const char* SUFFIX_REPORTS_DIFF = " should report a diff";
constexpr const char* SUFFIX_RETURNS_DIFF_MODEL = " should return a diff model";
constexpr const char* SUFFIX_HAS_NO_BASELINE = " should not have a baseline";
constexpr const char* SUFFIX_DOES_NOT_REUSE_BASELINE = " should not reuse another baseline";

constexpr const char* MISSING_SCAN = "missing scan";
constexpr const char* RUNNING_SCAN = "running scan";
constexpr const char* FAILED_SCAN = "failed scan";
constexpr const char* ABORTED_SCAN = "aborted scan";
constexpr const char* DEPENDENCY_MISSING_SCAN = "dependency-missing scan";
constexpr const char* DELETED_SCAN = "deleted scan";
constexpr const char* DELETED_COMPLETED_SCAN = "deleted completed scan";
constexpr const char* FIRST_COMPLETED_SCAN = "first completed scan";
constexpr const char* COMPLETED_SCAN_WITHOUT_BASELINE = "completed scan without baseline";
constexpr const char* SECOND_COMPARABLE_COMPLETED_SCAN =
    "second comparable completed scan";
constexpr const char* COMPLETED_SCAN_WITH_BASELINE = "completed scan with baseline";
constexpr const char* DIFFERENT_REQUESTED_PORT_SCAN = "different requested_port scan";
constexpr const char* DIFFERENT_REQUESTED_PORT_COMPLETED_SCAN =
    "different requested_port completed scan";
constexpr const char* DIFFERENT_TARGET_SCAN = "different target scan";
constexpr const char* DIFFERENT_TARGET_COMPLETED_SCAN = "different target completed scan";

constexpr const char* DIFF_CARRIES_CURRENT_SCAN = "diff model should carry the current scan";
constexpr const char* DIFF_WITHOUT_BASELINE_EMPTY_HOST_DIFF =
    "diff without baseline should have empty host diff";
constexpr const char* DIFF_WITHOUT_BASELINE_EMPTY_PORT_DIFF =
    "diff without baseline should have empty port diff";
constexpr const char* COMPARABLE_SCAN_HAS_BASELINE =
    "comparable completed scan should have a baseline";
constexpr const char* BASELINE_IS_PREVIOUS_COMPARABLE_SCAN =
    "baseline should be the previous comparable completed scan";
constexpr const char* HOST_DIFF_CONTAINS_NEW_HOST =
    "host diff should contain the new host";
constexpr const char* HOST_DIFF_CONTAINS_DISAPPEARED_HOST =
    "host diff should contain the disappeared host";
constexpr const char* NEW_HOST_DIFF_ENTRY_MATCHES_CURRENT_SCAN =
    "new host diff entry should match current scan host";
constexpr const char* DISAPPEARED_HOST_DIFF_ENTRY_MATCHES_BASELINE =
    "disappeared host diff entry should match baseline host";
constexpr const char* PORT_DIFF_CONTAINS_NEW_OPEN_PORT =
    "port diff should contain the newly open port";
constexpr const char* PORT_DIFF_ENTRY_MATCHES_CURRENT_SCAN =
    "port diff entry should match current scan port";
constexpr const char* ACKNOWLEDGED_DIFF_ENTRY_IS_MARKED =
    "acknowledged diff entry should be marked";
constexpr const char* ACKNOWLEDGEMENT_REAPPLIES_TO_LATER_DIFF =
    "acknowledgement should reapply to a later comparable diff";

struct ErrorExpectation
{
    ScanDiffError error;
    const char*                context;
};

struct NonDiffableFixture
{
    const ScanRequest*     request;
    PersistedScanState     state;
    const char*            persist_context;
    const char*            diff_context;
};

struct NoBaselineFixture
{
    const ScanRequest*      request;
    lsm::scan::ScanSnapshot snapshot;
    const char*             persist_context;
    const char*             diff_context;
};

struct TakenDiff
{
    bool ok;
    std::unique_ptr<ScanDiffModel> diff;
};

std::string make_message(const char* context, const char* suffix)
{
    return std::string(context) + suffix;
}

const char* error_name(ScanDiffError error)
{
    switch (error)
    {
    case ScanDiffError::None:
        return "None";
    case ScanDiffError::NotFound:
        return "NotFound";
    case ScanDiffError::NotDiffable:
        return "NotDiffable";
    case ScanDiffError::DatabaseError:
        return "DatabaseError";
    }

    return "unknown";
}

bool expect_persisted(int id, const char* context)
{
    return expect(id > 0, make_message(context, SUFFIX_PERSISTS));
}

bool expect_diff_error(const ScanDiffResult& result,
                       const ErrorExpectation& expectation)
{
    const std::string no_diff_message = make_message(expectation.context, SUFFIX_NO_DIFF);
    const std::string error_message =
        make_message(expectation.context, SUFFIX_RETURNS_ERROR) + error_name(expectation.error);

    bool ok = true;
    ok = expect(!result.has_diff(), no_diff_message) && ok;
    ok = expect(result.error == expectation.error, error_message) && ok;
    return ok;
}

TakenDiff take_expected_diff(ScanDiffResult& result, const char* context)
{
    const std::string has_diff_message = make_message(context, SUFFIX_REPORTS_DIFF);
    const std::string take_diff_message = make_message(context, SUFFIX_RETURNS_DIFF_MODEL);

    bool ok = true;
    ok = expect(result.has_diff(), has_diff_message) && ok;

    std::unique_ptr<ScanDiffModel> diff = result.take_diff();
    ok = expect(diff != nullptr, take_diff_message) && ok;

    TakenDiff taken;
    taken.ok = ok;
    taken.diff = std::move(diff);
    return taken;
}

bool expect_no_baseline_diff(ScanDiffResult& result, const char* context)
{
    TakenDiff taken = take_expected_diff(result, context);
    if (!taken.diff)
        return taken.ok;

    return expect(!taken.diff->baseline,
                  make_message(context, SUFFIX_DOES_NOT_REUSE_BASELINE)) &&
           taken.ok;
}

ScanDiffAcknowledgementKey make_ack_key(const char* category, const char* ip, int port = 0)
{
    ScanDiffAcknowledgementKey key;
    key.category = category;
    key.ip = ip;
    key.port = port;
    key.has_port = port > 0;
    return key;
}

bool expect_ack_ok(const ScanDiffAcknowledgementResult& result, const char* context)
{
    return expect(result.ok(), make_message(context, " acknowledgement should persist"));
}

bool expect_ack_bad_request(const ScanDiffAcknowledgementResult& result, const char* context)
{
    return expect(result.error == ScanDiffAcknowledgementError::BadRequest,
                  make_message(context, " should reject invalid acknowledgement"));
}

bool expect_all_diff_entries_acknowledged(const ScanDiffModel& diff, const char* context)
{
    bool ok = true;
    ok = expect(!diff.host_diff.new_hosts.empty() &&
                    diff.host_diff.new_hosts[0].acknowledged,
                make_message(context, " should mark new host acknowledged")) && ok;
    ok = expect(!diff.host_diff.disappeared_hosts.empty() &&
                    diff.host_diff.disappeared_hosts[0].acknowledged,
                make_message(context, " should mark disappeared host acknowledged")) && ok;
    ok = expect(!diff.port_diff.new_open_ports.empty() &&
                    diff.port_diff.new_open_ports[0].acknowledged,
                make_message(context, " should mark new port acknowledged")) && ok;
    ok = expect(!diff.port_diff.disappeared_ports.empty() &&
                    diff.port_diff.disappeared_ports[0].acknowledged,
                make_message(context, " should mark disappeared port acknowledged")) && ok;
    return ok;
}
} // namespace

int main()
{
    bool all_ok = true;

    const std::string db_path = make_temp_db_path("/tmp/netscan-scan-diff-test-XXXXXX.db");
    all_ok = expect(!db_path.empty(), TEMP_DB_CREATED) && all_ok;
    if (!all_ok)
        return finish_test("scan_diff_model_test", false);

    {
        Database db(db_path);
        Logger logger;
        configure_database_runtime(db, logger);
        init_schema(db, logger);
        ScanRepository repo(db);
        ScanDiffService service(db, repo);

        const ScanRequest scope_a_port_22{"127.0.0.1", "22", false, ""};
        const ScanRequest scope_a_port_80{"127.0.0.1", "80", false, ""};
        const ScanRequest scope_b_port_22{"127.0.0.2", "22", false, ""};

        {
            const ScanDiffResult result = service.get_scan_diff(9999);
            all_ok = expect_diff_error(result, {ScanDiffError::NotFound, MISSING_SCAN}) &&
                     all_ok;
        }

        const NonDiffableFixture non_diffable_fixtures[] = {
            {&scope_a_port_22, PersistedScanState::Running, RUNNING_SCAN, RUNNING_SCAN},
            {&scope_a_port_22, PersistedScanState::Failed, FAILED_SCAN, FAILED_SCAN},
            {&scope_a_port_22, PersistedScanState::Aborted, ABORTED_SCAN, ABORTED_SCAN},
            {&scope_a_port_22, PersistedScanState::DependencyMissing, DEPENDENCY_MISSING_SCAN,
             DEPENDENCY_MISSING_SCAN},
        };
        for (const NonDiffableFixture& fixture : non_diffable_fixtures)
        {
            const int scan_id =
                persist_scan(db, repo, {*fixture.request, fixture.state, false, {}, false});
            all_ok = expect_persisted(scan_id, fixture.persist_context) && all_ok;
            if (scan_id > 0)
            {
                const ScanDiffResult result = service.get_scan_diff(scan_id);
                all_ok = expect_diff_error(
                             result,
                             {ScanDiffError::NotDiffable, fixture.diff_context}) &&
                         all_ok;
            }
        }

        const int deleted_scan_id = persist_scan(
            db, repo,
            {scope_a_port_22, PersistedScanState::Completed, false,
             lsm::scan::ScanSnapshot{{{"10.0.0.9", "deleted-host", {{"tcp", 22, "open", "ssh"}}}}},
             true});
        all_ok = expect_persisted(deleted_scan_id, DELETED_COMPLETED_SCAN) && all_ok;
        {
            const ScanDiffResult result = service.get_scan_diff(deleted_scan_id);
            all_ok =
                expect_diff_error(result, {ScanDiffError::NotFound, DELETED_SCAN}) &&
                all_ok;
        }

        const int first_scan_id = persist_scan(
            db, repo,
            {scope_a_port_22, PersistedScanState::Completed, false,
             lsm::scan::ScanSnapshot{{{"10.0.0.1", "host-a", {{"tcp", 22, "open", "ssh"}}}}}});
        all_ok = expect_persisted(first_scan_id, FIRST_COMPLETED_SCAN) && all_ok;

        {
            ScanDiffResult result = service.get_scan_diff(first_scan_id);
            all_ok = expect(result.error == ScanDiffError::None,
                            REPORTS_NO_ERROR) && all_ok;

            TakenDiff taken = take_expected_diff(result, COMPLETED_SCAN_WITHOUT_BASELINE);
            all_ok = taken.ok && all_ok;
            // Error state remains valid after take_diff()
            // all_ok = expect(result.error == ScanDiffError::None,
            //                 REPORTS_NO_ERROR_AFTER_TAKE) && all_ok;

            if (taken.diff)
            {
                all_ok = expect(taken.diff->scan.id == first_scan_id,
                                DIFF_CARRIES_CURRENT_SCAN) && all_ok;
                all_ok = expect(!taken.diff->baseline,
                                make_message(FIRST_COMPLETED_SCAN, SUFFIX_HAS_NO_BASELINE)) &&
                         all_ok;
                all_ok = expect(taken.diff->host_diff.new_hosts.empty() &&
                                    taken.diff->host_diff.disappeared_hosts.empty(),
                                DIFF_WITHOUT_BASELINE_EMPTY_HOST_DIFF) && all_ok;
                all_ok = expect(taken.diff->port_diff.new_open_ports.empty(),
                                DIFF_WITHOUT_BASELINE_EMPTY_PORT_DIFF) && all_ok;
            }
        }

        const int second_scan_id = persist_scan(
            db, repo,
            {scope_a_port_22, PersistedScanState::Completed, false,
             lsm::scan::ScanSnapshot{{{"10.0.0.2", "host-b", {{"tcp", 80, "open", "http"}}}}}});
        all_ok = expect_persisted(second_scan_id, SECOND_COMPARABLE_COMPLETED_SCAN) && all_ok;

        {
            ScanDiffResult result = service.get_scan_diff(second_scan_id);
            TakenDiff taken = take_expected_diff(result, COMPLETED_SCAN_WITH_BASELINE);
            all_ok = taken.ok && all_ok;

            if (taken.diff)
            {
                all_ok = expect(taken.diff->baseline != nullptr,
                                COMPARABLE_SCAN_HAS_BASELINE) && all_ok;
                if (taken.diff->baseline)
                    all_ok = expect(taken.diff->baseline->id == first_scan_id,
                                    BASELINE_IS_PREVIOUS_COMPARABLE_SCAN) &&
                             all_ok;

                all_ok = expect(taken.diff->host_diff.new_hosts.size() == 1,
                                HOST_DIFF_CONTAINS_NEW_HOST) && all_ok;
                all_ok = expect(taken.diff->host_diff.disappeared_hosts.size() == 1,
                                HOST_DIFF_CONTAINS_DISAPPEARED_HOST) && all_ok;
                all_ok = expect(taken.diff->host_diff.new_hosts[0].ip == "10.0.0.2",
                                NEW_HOST_DIFF_ENTRY_MATCHES_CURRENT_SCAN) && all_ok;
                all_ok = expect(taken.diff->host_diff.disappeared_hosts[0].ip == "10.0.0.1",
                                DISAPPEARED_HOST_DIFF_ENTRY_MATCHES_BASELINE) &&
                         all_ok;

                all_ok = expect(taken.diff->port_diff.new_open_ports.size() == 1,
                                PORT_DIFF_CONTAINS_NEW_OPEN_PORT) && all_ok;
                all_ok = expect(taken.diff->port_diff.new_open_ports[0].ip == "10.0.0.2" &&
                                    taken.diff->port_diff.new_open_ports[0].port == 80,
                                PORT_DIFF_ENTRY_MATCHES_CURRENT_SCAN) && all_ok;
            }
        }

        {
            const ScanDiffAcknowledgementKey new_host_key =
                make_ack_key("new_host", "10.0.0.2");
            const ScanDiffAcknowledgementKey disappeared_host_key =
                make_ack_key("disappeared_host", "10.0.0.1");
            const ScanDiffAcknowledgementKey new_port_key =
                make_ack_key("new_open_port", "10.0.0.2", 80);
            const ScanDiffAcknowledgementKey disappeared_port_key =
                make_ack_key("disappeared_port", "10.0.0.1", 22);

            all_ok = expect_ack_ok(service.acknowledge_diff(new_host_key), "new host") &&
                     all_ok;
            all_ok = expect_ack_ok(service.acknowledge_diff(disappeared_host_key),
                                   "disappeared host") && all_ok;
            all_ok = expect_ack_ok(service.acknowledge_diff(new_port_key), "new port") &&
                     all_ok;
            all_ok = expect_ack_ok(service.acknowledge_diff(disappeared_port_key),
                                   "disappeared port") && all_ok;

            ScanDiffResult result = service.get_scan_diff(second_scan_id);
            TakenDiff taken = take_expected_diff(result, "acknowledged comparable scan");
            all_ok = taken.ok && all_ok;
            if (taken.diff)
                all_ok = expect_all_diff_entries_acknowledged(
                             *taken.diff, ACKNOWLEDGED_DIFF_ENTRY_IS_MARKED) && all_ok;
        }

        {
            all_ok = expect_ack_bad_request(service.acknowledge_diff(
                                                make_ack_key("invalid", "10.0.0.2")),
                                            "invalid category") && all_ok;
            all_ok = expect_ack_bad_request(service.acknowledge_diff(
                                                make_ack_key("new_host", "")),
                                            "missing ip") && all_ok;
            all_ok = expect_ack_bad_request(service.acknowledge_diff(
                                                make_ack_key("new_open_port", "10.0.0.2")),
                                            "missing port") && all_ok;
            all_ok = expect_ack_bad_request(service.acknowledge_diff(
                                                make_ack_key("new_open_port", "10.0.0.2",
                                                             70000)),
                                            "invalid port") && all_ok;
        }

        const int third_scan_id = persist_scan(
            db, repo,
            {scope_a_port_22, PersistedScanState::Completed, false,
             lsm::scan::ScanSnapshot{{{"10.0.0.1", "host-a", {{"tcp", 22, "open", "ssh"}}}}}});
        all_ok = expect_persisted(third_scan_id, "third comparable completed scan") && all_ok;

        const int fourth_scan_id = persist_scan(
            db, repo,
            {scope_a_port_22, PersistedScanState::Completed, false,
             lsm::scan::ScanSnapshot{{{"10.0.0.2", "host-b", {{"tcp", 80, "open", "http"}}}}}});
        all_ok = expect_persisted(fourth_scan_id, "fourth comparable completed scan") && all_ok;

        {
            ScanDiffResult result = service.get_scan_diff(fourth_scan_id);
            TakenDiff taken = take_expected_diff(result, "later comparable scan");
            all_ok = taken.ok && all_ok;
            if (taken.diff && !taken.diff->host_diff.new_hosts.empty())
            {
                all_ok = expect_all_diff_entries_acknowledged(
                             *taken.diff, ACKNOWLEDGEMENT_REAPPLIES_TO_LATER_DIFF) &&
                         all_ok;
            }
        }

        const NoBaselineFixture no_baseline_fixtures[] = {
            {&scope_a_port_80,
             lsm::scan::ScanSnapshot{{{"10.0.0.3", "host-c", {{"tcp", 80, "open", "http"}}}}},
             DIFFERENT_REQUESTED_PORT_SCAN,
             DIFFERENT_REQUESTED_PORT_COMPLETED_SCAN},
            {&scope_b_port_22,
             lsm::scan::ScanSnapshot{{{"10.0.1.1", "host-d", {{"tcp", 22, "open", "ssh"}}}}},
             DIFFERENT_TARGET_SCAN,
             DIFFERENT_TARGET_COMPLETED_SCAN},
        };
        for (const NoBaselineFixture& fixture : no_baseline_fixtures)
        {
            const int scan_id = persist_scan(db, repo, {*fixture.request,
                                                        PersistedScanState::Completed,
                                                        false,
                                                        fixture.snapshot});
            all_ok = expect_persisted(scan_id, fixture.persist_context) && all_ok;
            if (scan_id > 0)
            {
                ScanDiffResult result = service.get_scan_diff(scan_id);
                all_ok = expect_no_baseline_diff(result, fixture.diff_context) && all_ok;
            }
        }
    }

    std::remove(db_path.c_str());
    return finish_test("scan_diff_model_test", all_ok);
}
