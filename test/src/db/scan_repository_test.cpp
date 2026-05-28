#include "db/database.hpp"
#include "db/schema.hpp"
#include "db/scan_repository.hpp"
#include "scan/nmap_xml_parser.hpp"
#include "scan_service_test_support.hpp"
#include "test_output.hpp"
#include "util/logger.hpp"

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace
{
bool expect_scan_fields(const PersistedScanSummary& scan, PersistedScanState expected_state,
                        const ScanResult& expected_result, const std::string& expected_finished_at)
{
    bool ok = true;
    ok = expect(scan.state == expected_state, "scan should persist the expected terminal state") &&
         ok;
    ok = expect(scan.message == expected_result.message,
                "scan should persist the expected terminal message") &&
         ok;
    ok = expect(scan.command == expected_result.command,
                "scan should persist the expected terminal command") &&
         ok;
    ok = expect(scan.finished_at == expected_finished_at,
                "scan should persist the expected finished timestamp") &&
         ok;

    if (expected_result.exit_code >= 0)
    {
        ok = expect(scan.has_exit_code, "scan should persist an exit code when one is provided") &&
             ok;
        ok = expect(scan.exit_code == expected_result.exit_code,
                    "scan should persist the expected exit code") &&
             ok;
    }
    else
    {
        ok = expect(!scan.has_exit_code,
                    "scan should store a null exit code when no exit code is provided") &&
             ok;
    }

    return ok;
}

bool expect_fake_nmap_xml_hosts_are_persisted(Database& db, ScanRepository& repo)
{
    const char* xml = R"XML(
<?xml version="1.0"?>
<nmaprun>
<host>
    <status state="up"/>
    <address addr="10.0.0.10" addrtype="ipv4"/>
    <hostnames>
        <hostname name="empty-ports.local"/>
    </hostnames>
    <ports>
    </ports>
</host>
<host>
    <status state="up"/>
    <address addr="10.0.0.11" addrtype="ipv4"/>
    <ports>
        <port protocol="tcp" portid="22">
            <state state="open"/>
            <service name="ssh"/>
        </port>
    </ports>
</host>
<host>
    <status state="up"/>
    <address addr="10.0.0.12" addrtype="ipv4"/>
</host>
</nmaprun>
)XML";

    bool all_ok = true;
    lsm::scan::NmapXmlParseResult parse_result = lsm::scan::parse_nmap_xml(xml);
    all_ok = expect(parse_result.ok, "fake nmap XML should parse") && all_ok;
    all_ok = expect(parse_result.snapshot.hosts.size() == 3,
                    "fake nmap XML should include every up host before persistence") &&
             all_ok;

    const int scan_id = persist_scan(
        db, repo,
        {{"10.0.0.0/24", "22", false, ""}, PersistedScanState::Completed, false,
         parse_result.snapshot});
    all_ok = expect(scan_id > 0, "fake nmap XML snapshot should persist") && all_ok;

    std::vector<ScanHostRow> hosts;
    std::vector<ScanPortRow> ports;
    db.read(
        [&](sqlite3* h)
        {
            hosts = repo.list_scan_hosts(h, scan_id);
            ports = repo.list_scan_ports(h, scan_id);
        });

    all_ok = expect(hosts.size() == 3, "persisted snapshot should retain up hosts with no ports") &&
             all_ok;
    if (hosts.size() == 3)
    {
        all_ok = expect(hosts[0].ip == "10.0.0.10" && hosts[0].name == "empty-ports.local",
                        "first persisted host should match the zero-port nmap host") &&
                 all_ok;
        all_ok = expect(hosts[1].ip == "10.0.0.11",
                        "second persisted host should match the open-port nmap host") &&
                 all_ok;
        all_ok = expect(hosts[2].ip == "10.0.0.12",
                        "third persisted host should match the no-ports-node nmap host") &&
                 all_ok;
    }

    all_ok = expect(ports.size() == 1, "only the host with an open port should persist a port") &&
             all_ok;
    if (ports.size() == 1)
    {
        all_ok = expect(ports[0].host_ip == "10.0.0.11" && ports[0].port == 22 &&
                            ports[0].protocol == "tcp" && ports[0].state == "open" &&
                            ports[0].service == "ssh",
                        "persisted port should retain the nmap port details") &&
                 all_ok;
    }

    return all_ok;
}

bool expect_diff_acknowledgement_persists(Database& db, ScanRepository& repo)
{
    ScanDiffAcknowledgementKey key;
    key.category = "new_open_port";
    key.ip = "10.0.0.20";
    key.port = 443;
    key.has_port = true;

    bool all_ok = true;
    bool set_ok = false;
    db.write([&](sqlite3* h) { set_ok = repo.set_diff_acknowledgement(h, key); });
    all_ok = expect(set_ok, "diff acknowledgement should persist") && all_ok;

    std::unordered_set<std::string> keys;
    db.read([&](sqlite3* h) { keys = repo.list_acknowledged_diff_keys(h); });
    all_ok = expect(keys.find(scan_diff_acknowledgement_key_id(key)) != keys.end(),
                    "persisted diff acknowledgement should be readable") &&
             all_ok;

    bool clear_ok = false;
    db.write([&](sqlite3* h) { clear_ok = repo.clear_diff_acknowledgement(h, key); });
    all_ok = expect(clear_ok, "diff acknowledgement should clear") && all_ok;

    db.read([&](sqlite3* h) { keys = repo.list_acknowledged_diff_keys(h); });
    all_ok = expect(keys.find(scan_diff_acknowledgement_key_id(key)) == keys.end(),
                    "cleared diff acknowledgement should not be readable") &&
             all_ok;
    return all_ok;
}

const PersistedPortStateRow* find_port_state(const std::vector<PersistedPortStateRow>& rows, int port)
{
    for (std::vector<PersistedPortStateRow>::const_iterator it = rows.begin(); it != rows.end(); ++it)
    {
        if (it->port == port)
            return &(*it);
    }
    return nullptr;
}

bool expect_scan_port_coverage_persists(Database& db, ScanRepository& repo)
{
    bool all_ok = true;

    const int full_scan_id = persist_scan(
        db, repo, {{"10.10.0.1", "", false, ""}, PersistedScanState::Completed, false,
                   lsm::scan::ScanSnapshot{{{"10.10.0.1", "", {{"tcp", 22, "open", "ssh"}}}}}});
    all_ok = expect(full_scan_id > 0, "full port scan fixture should persist") && all_ok;

    const int partial_scan_id = persist_scan(
        db, repo, {{"10.10.0.2", "22,80,443,1000-1010", false, ""},
                   PersistedScanState::Completed, false,
                   lsm::scan::ScanSnapshot{{{"10.10.0.2", "", {{"tcp", 22, "open", "ssh"}}}}}});
    all_ok = expect(partial_scan_id > 0, "partial port scan fixture should persist") && all_ok;

    const int host_scan_id = persist_scan(
        db, repo, {{"10.10.0.3", "", true, ""}, PersistedScanState::Completed, true,
                   lsm::scan::ScanSnapshot{{{"10.10.0.3", "", {}}}}});
    all_ok = expect(host_scan_id > 0, "host discovery fixture should persist") && all_ok;

    std::vector<ScanPortCoverageRow> full_ranges;
    std::vector<ScanPortCoverageRow> partial_ranges;
    std::vector<ScanPortCoverageRow> host_ranges;
    bool full_known = false;
    bool partial_known = false;
    bool host_known = false;
    bool full_covers_65535 = false;
    bool partial_covers_1005 = false;
    bool partial_covers_999 = true;
    bool host_covers_22 = true;
    db.read([&](sqlite3* h) {
        full_ranges = repo.list_scan_port_coverage(h, full_scan_id);
        partial_ranges = repo.list_scan_port_coverage(h, partial_scan_id);
        host_ranges = repo.list_scan_port_coverage(h, host_scan_id);
        full_known = repo.scan_has_known_port_coverage(h, full_scan_id);
        partial_known = repo.scan_has_known_port_coverage(h, partial_scan_id);
        host_known = repo.scan_has_known_port_coverage(h, host_scan_id);
        full_covers_65535 = repo.scan_covers_port(h, full_scan_id, 65535);
        partial_covers_1005 = repo.scan_covers_port(h, partial_scan_id, 1005);
        partial_covers_999 = repo.scan_covers_port(h, partial_scan_id, 999);
        host_covers_22 = repo.scan_covers_port(h, host_scan_id, 22);
    });

    all_ok = expect(full_known, "full port scan should mark coverage as known") && all_ok;
    all_ok = expect(partial_known, "partial port scan should mark coverage as known") && all_ok;
    all_ok = expect(host_known, "host discovery scan should mark coverage as known") && all_ok;
    all_ok = expect(full_ranges.size() == 1 && full_ranges[0].start_port == 1 &&
                        full_ranges[0].end_port == 65535,
                    "full port scan should persist one all-port coverage range") &&
             all_ok;
    all_ok = expect(partial_ranges.size() == 4, "partial port scan should persist normalized ranges") &&
             all_ok;
    if (partial_ranges.size() == 4)
    {
        all_ok = expect(partial_ranges[0].start_port == 22 && partial_ranges[0].end_port == 22,
                        "partial coverage should retain single-port ranges") &&
                 all_ok;
        all_ok = expect(partial_ranges[3].start_port == 1000 && partial_ranges[3].end_port == 1010,
                        "partial coverage should retain explicit port spans") &&
                 all_ok;
    }
    all_ok = expect(host_ranges.empty(),
                    "host discovery scan should persist known empty port coverage") &&
             all_ok;
    all_ok = expect(full_covers_65535, "full port scan should cover port 65535") && all_ok;
    all_ok = expect(partial_covers_1005, "partial port scan should cover ports inside saved ranges") &&
             all_ok;
    all_ok = expect(!partial_covers_999,
                    "partial port scan should not cover ports outside saved ranges") &&
             all_ok;
    all_ok = expect(!host_covers_22,
                    "host discovery scan should not report any covered TCP ports") &&
             all_ok;

    return all_ok;
}

bool expect_port_state_tracks_coverage(Database& db, ScanRepository& repo)
{
    bool all_ok = true;

    const std::string host_ip = "10.20.0.10";
    const int first_scan_id = persist_scan(
        db, repo, {{host_ip, "22", false, ""}, PersistedScanState::Completed, false,
                   lsm::scan::ScanSnapshot{{{host_ip, "tracked-host",
                                             {{"tcp", 22, "open", "ssh"}}}}}});
    all_ok = expect(first_scan_id > 0, "first tracked port scan should persist") && all_ok;

    const int covered_closed_scan_id = persist_scan(
        db, repo, {{host_ip, "22,80", false, ""}, PersistedScanState::Completed, false,
                   lsm::scan::ScanSnapshot{{{host_ip, "tracked-host", {}}}}});
    all_ok = expect(covered_closed_scan_id > 0,
                    "covered closed follow-up scan should persist") &&
             all_ok;

    const std::string uncovered_host_ip = "10.20.0.11";
    const int uncovered_first_scan_id = persist_scan(
        db, repo, {{uncovered_host_ip, "22", false, ""}, PersistedScanState::Completed, false,
                   lsm::scan::ScanSnapshot{{{uncovered_host_ip, "uncovered-host",
                                             {{"tcp", 22, "open", "ssh"}}}}}});
    all_ok = expect(uncovered_first_scan_id > 0,
                    "uncovered first tracked port scan should persist") &&
             all_ok;

    const int uncovered_followup_scan_id = persist_scan(
        db, repo, {{uncovered_host_ip, "443", false, ""}, PersistedScanState::Completed, false,
                   lsm::scan::ScanSnapshot{{{uncovered_host_ip, "uncovered-host", {}}}}});
    all_ok = expect(uncovered_followup_scan_id > 0,
                    "non-covering follow-up scan should persist") &&
             all_ok;

    int tracked_host_id = -1;
    int uncovered_host_id = -1;
    std::vector<PersistedPortStateRow> tracked_states;
    std::vector<PersistedPortStateRow> uncovered_states;
    db.read([&](sqlite3* h) {
        tracked_host_id = repo.get_host_id_by_ip(h, host_ip);
        uncovered_host_id = repo.get_host_id_by_ip(h, uncovered_host_ip);
        tracked_states = repo.list_current_ports_for_host(h, tracked_host_id);
        uncovered_states = repo.list_current_ports_for_host(h, uncovered_host_id);
    });

    all_ok = expect(tracked_host_id > 0, "tracked host id should resolve") && all_ok;
    all_ok = expect(uncovered_host_id > 0, "uncovered host id should resolve") && all_ok;

    const PersistedPortStateRow* tracked_port_22 = find_port_state(tracked_states, 22);
    all_ok = expect(tracked_port_22 != nullptr,
                    "covered closed follow-up should retain tracked host-port state") &&
             all_ok;
    if (tracked_port_22)
    {
        all_ok = expect(tracked_port_22->state == "closed",
                        "covered missing port should become closed in current state") &&
                 all_ok;
        all_ok = expect(tracked_port_22->last_open_scan_id == first_scan_id,
                        "covered missing port should retain the last open evidence scan") &&
                 all_ok;
        all_ok = expect(tracked_port_22->last_observed_scan_id == covered_closed_scan_id,
                        "covered missing port should record the last closing coverage scan") &&
                 all_ok;
    }

    const PersistedPortStateRow* uncovered_port_22 = find_port_state(uncovered_states, 22);
    all_ok = expect(uncovered_port_22 != nullptr,
                    "non-covering follow-up should retain tracked host-port state") &&
             all_ok;
    if (uncovered_port_22)
    {
        all_ok = expect(uncovered_port_22->state == "open",
                        "non-covering follow-up should keep the last known open state") &&
                 all_ok;
        all_ok = expect(uncovered_port_22->last_open_scan_id == uncovered_first_scan_id,
                        "non-covering follow-up should preserve the open evidence scan") &&
                 all_ok;
        all_ok = expect(uncovered_port_22->last_observed_scan_id == uncovered_first_scan_id,
                        "non-covering follow-up should not fake a later coverage observation") &&
                 all_ok;
    }

    return all_ok;
}
} // namespace

int main()
{
    bool all_ok = true;

    const std::string db_path = make_temp_db_path("/tmp/netscan-scan-repository-test-XXXXXX.db");
    all_ok = expect(!db_path.empty(), "temporary database path should be created") && all_ok;
    if (!all_ok)
        return finish_test("scan_repository_test", false);

    Database db(db_path);
    Logger logger;
    configure_database_runtime(db, logger);
    init_schema(db, logger);

    ScanRepository repo(db);

    const ScanRequest request = {"127.0.0.1", "443", false, ""};
    int completed_scan_id = -1;
    db.write([&](sqlite3* h) { completed_scan_id = repo.insert_scan_queued(h, request); });
    all_ok = expect(completed_scan_id > 0, "queued scan should be inserted for completed update") &&
             all_ok;

    bool mark_running_ok = false;
    db.write(
        [&](sqlite3* h)
        {
            mark_running_ok =
                repo.mark_scan_running(h, completed_scan_id, "2026-01-01 00:00:01");
        });
    all_ok =
        expect(mark_running_ok, "queued scan should transition to running before completion") &&
        all_ok;

    const ScanResult completed_result = {ScanOutcome::Completed, "scan completed", "nmap -Pn", 0,
                                         "", ""};
    const std::string completed_finished_at = "2026-01-01 00:00:02";
    bool mark_completed_ok = false;
    db.write(
        [&](sqlite3* h)
        {
            mark_completed_ok = repo.mark_scan_completed(h, completed_scan_id, completed_result,
                                                         completed_finished_at);
        });
    all_ok = expect(mark_completed_ok, "mark_scan_completed should succeed for an existing scan") &&
             all_ok;

    std::unique_ptr<PersistedScanSummary> completed_scan;
    db.read([&](sqlite3* h) { completed_scan = repo.get_scan_by_id(h, completed_scan_id); });
    all_ok = expect(completed_scan != nullptr, "completed scan should still be readable") && all_ok;
    if (completed_scan)
        all_ok = expect_scan_fields(*completed_scan, PersistedScanState::Completed,
                                    completed_result, completed_finished_at) &&
                 all_ok;

    int dependency_scan_id = -1;
    db.write(
        [&](sqlite3* h)
        { dependency_scan_id = repo.insert_scan_queued(h, {"127.0.0.2", "", true, ""}); });
    all_ok =
        expect(dependency_scan_id > 0, "queued scan should be inserted for dependency update") &&
        all_ok;

    const ScanResult dependency_result = {ScanOutcome::DependencyMissing, "nmap not installed", "",
                                          -1, "", ""};
    const std::string dependency_finished_at = "2026-01-01 00:00:03";
    bool mark_dependency_running_ok = false;
    db.write(
        [&](sqlite3* h)
        {
            mark_dependency_running_ok =
                repo.mark_scan_running(h, dependency_scan_id, "2026-01-01 00:00:02");
        });
    all_ok = expect(mark_dependency_running_ok,
                    "dependency scan should transition to running before terminal update") &&
             all_ok;

    bool mark_dependency_ok = false;
    db.write(
        [&](sqlite3* h)
        {
            mark_dependency_ok = repo.mark_scan_dependency_missing(
                h, dependency_scan_id, dependency_result, dependency_finished_at);
        });
    all_ok = expect(mark_dependency_ok,
                    "mark_scan_dependency_missing should succeed for an existing scan") &&
             all_ok;

    std::unique_ptr<PersistedScanSummary> dependency_scan;
    db.read([&](sqlite3* h) { dependency_scan = repo.get_scan_by_id(h, dependency_scan_id); });
    all_ok =
        expect(dependency_scan != nullptr, "dependency-missing scan should still be readable") &&
        all_ok;
    if (dependency_scan)
        all_ok = expect_scan_fields(*dependency_scan, PersistedScanState::DependencyMissing,
                                    dependency_result, dependency_finished_at) &&
                 all_ok;

    bool mark_failed_fails = false;
    db.write(
        [&](sqlite3* h)
        {
            mark_failed_fails = !repo.mark_scan_failed(
                h, 999999, {ScanOutcome::Failed, "missing scan", "", -1, "", ""},
                "2026-01-01 00:00:04");
        });
    all_ok = expect(mark_failed_fails,
                    "terminal status updates should return false for a missing scan") &&
             all_ok;

    // mark_scan_aborted: happy path — running → aborted, timestamps and exit_code set
    int aborted_scan_id = -1;
    db.write([&](sqlite3* h) { aborted_scan_id = repo.insert_scan_queued(h, {"10.0.0.1", "", false, ""}); });
    all_ok = expect(aborted_scan_id > 0, "aborted scan fixture should be inserted") && all_ok;

    db.write([&](sqlite3* h) { repo.mark_scan_running(h, aborted_scan_id, "2026-01-01 01:00:00"); });

    const ScanResult aborted_result = {ScanOutcome::Aborted, "user aborted", "nmap -sn", 1, "", ""};
    const std::string aborted_finished_at = "2026-01-01 01:00:05";
    bool mark_aborted_ok = false;
    db.write([&](sqlite3* h) {
        mark_aborted_ok = repo.mark_scan_aborted(h, aborted_scan_id, aborted_result, aborted_finished_at);
    });
    all_ok = expect(mark_aborted_ok, "mark_scan_aborted should succeed for a running scan") && all_ok;

    std::unique_ptr<PersistedScanSummary> aborted_scan;
    db.read([&](sqlite3* h) { aborted_scan = repo.get_scan_by_id(h, aborted_scan_id); });
    all_ok = expect(aborted_scan != nullptr, "aborted scan should still be readable") && all_ok;
    if (aborted_scan)
        all_ok = expect_scan_fields(*aborted_scan, PersistedScanState::Aborted,
                                    aborted_result, aborted_finished_at) && all_ok;

    // mark_scan_aborted: idempotent — second call on already-aborted scan returns false
    bool second_abort_ok = true;
    db.write([&](sqlite3* h) {
        second_abort_ok = repo.mark_scan_aborted(h, aborted_scan_id, aborted_result, aborted_finished_at);
    });
    all_ok = expect(!second_abort_ok,
                    "mark_scan_aborted should return false when scan is no longer running") && all_ok;

    // soft_delete_scan_by_id: scan marked deleted, scan_hosts/scan_ports rows remain
    const int delete_scan_id = persist_scan(
        db, repo,
        {{"10.1.0.1", "22", false, ""}, PersistedScanState::Completed, false,
         lsm::scan::ScanSnapshot{{{"10.1.0.1", "host-a", {{"tcp", 22, "open", "ssh"}}}}}});
    all_ok = expect(delete_scan_id > 0, "delete test fixture scan should persist") && all_ok;

    bool soft_delete_ok = false;
    db.write([&](sqlite3* h) {
        soft_delete_ok = repo.soft_delete_scan_by_id(h, delete_scan_id, "2026-01-01 02:00:00");
    });
    all_ok = expect(soft_delete_ok, "soft_delete_scan_by_id should succeed for an existing scan") && all_ok;

    std::unique_ptr<PersistedScanSummary> deleted_scan;
    std::vector<ScanHostRow> deleted_hosts;
    std::vector<ScanPortRow> deleted_ports;
    db.read([&](sqlite3* h) {
        deleted_scan = repo.get_scan_by_id(h, delete_scan_id);
        deleted_hosts = repo.list_scan_hosts(h, delete_scan_id);
        deleted_ports = repo.list_scan_ports(h, delete_scan_id);
    });
    all_ok = expect(deleted_scan != nullptr && deleted_scan->deleted,
                    "soft-deleted scan should be readable and marked deleted") && all_ok;
    all_ok = expect(!deleted_hosts.empty(),
                    "scan_hosts rows should persist after soft delete (no cascade)") && all_ok;
    all_ok = expect(!deleted_ports.empty(),
                    "scan_ports rows should persist after soft delete (no cascade)") && all_ok;

    // soft_delete_scan_by_id: non-existent ID returns false (no-op)
    bool delete_missing_ok = true;
    db.write([&](sqlite3* h) {
        delete_missing_ok = repo.soft_delete_scan_by_id(h, 999999, "2026-01-01 03:00:00");
    });
    all_ok = expect(!delete_missing_ok,
                    "soft_delete_scan_by_id should return false for a non-existent scan") && all_ok;

    all_ok = expect_fake_nmap_xml_hosts_are_persisted(db, repo) && all_ok;
    all_ok = expect_scan_port_coverage_persists(db, repo) && all_ok;
    all_ok = expect_port_state_tracks_coverage(db, repo) && all_ok;
    all_ok = expect_diff_acknowledgement_persists(db, repo) && all_ok;

    return finish_test("scan_repository_test", all_ok);
}
