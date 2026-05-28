#include "db/database.hpp"
#include "db/schema.hpp"
#include "db/scan_repository.hpp"
#include "service/scan_service.hpp"
#include "service/settings_service.hpp"
#include "scan_service_test_support.hpp"
#include "test_output.hpp"
#include "util/logger.hpp"

#include <cstdio>
#include <fstream>
#include <memory>
#include <sqlite3.h>
#include <thread>
#include <string>
#include <vector>

int count_scan_rows(Database& db)
{
    return db.read([](sqlite3* h) {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(h, "SELECT COUNT(*) FROM scans;", -1, &stmt, nullptr) !=
            SQLITE_OK)
        {
            return -1;
        }

        int count = -1;
        if (sqlite3_step(stmt) == SQLITE_ROW)
            count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        return count;
    });
}

int main()
{
    bool all_ok = true;

    {
        ScanRuntime runtime;
        runtime.register_active(42, "127.0.0.1");

        PersistedScanSummary terminal;
        terminal.id = 42;
        terminal.target = "127.0.0.1";
        terminal.state = PersistedScanState::Aborted;
        terminal.message = "terminated by user";
        runtime.remember_transient_terminal(terminal);

        ScanRuntime::StatusSnapshot snapshot = runtime.get_status_snapshot();
        all_ok = expect(!snapshot.has_active,
                        "status snapshot should not expose active state when a matching transient terminal exists") &&
                 all_ok;
        all_ok = expect(snapshot.active_id == -1,
                        "status snapshot should clear the active id when the transient terminal supersedes it") &&
                 all_ok;
        all_ok = expect(snapshot.transient != nullptr,
                        "status snapshot should keep the transient terminal scan") &&
                 all_ok;
        if (snapshot.transient)
        {
            all_ok = expect(snapshot.transient->id == 42,
                            "status snapshot should preserve the transient terminal scan id") &&
                     all_ok;
            all_ok = expect(snapshot.transient->state == PersistedScanState::Aborted,
                            "status snapshot should preserve the transient terminal state") &&
                     all_ok;
        }
    }

    {
        ScanRuntime runtime;
        runtime.register_active(7, "192.168.1.0/26", true);

        NmapProcessHandle first;
        NmapProcessHandle second;
#ifdef _WIN32
        first.process = reinterpret_cast<HANDLE>(1);
        second.process = reinterpret_cast<HANDLE>(2);
#else
        first.pid = 1001;
        first.process_group_id = 1001;
        second.pid = 1002;
        second.process_group_id = 1002;
#endif
        runtime.register_process(7, first);
        runtime.register_process(7, second);

        all_ok = expect(runtime.active_chunked_scan_count() == 1,
                        "runtime should count active chunked scans") &&
                 all_ok;

        ScanRuntime::AbortContext ctx;
        all_ok = expect(runtime.request_abort(7, "terminated by user", ctx),
                        "runtime should accept abort for active chunked scan") &&
                 all_ok;
        all_ok = expect(ctx.handles.size() == 2,
                        "chunked scan abort should return every registered process handle") &&
                 all_ok;
    }

    const std::string db_path = make_temp_db_path("/tmp/netscan-scan-abort-service-test-XXXXXX.db");
    all_ok = expect(!db_path.empty(), "temporary database path should be created") && all_ok;
    if (!all_ok)
        return finish_test("scan_abort_service_test", false);

    {
        Database db(db_path);
        Logger logger;
        configure_database_runtime(db, logger);
        init_schema(db, logger);
        ScanRepository repo(db);

        const ScanRequest running_request{"127.0.0.1", "80", false, ""};
        const int queued_scan_id =
            persist_scan(db, repo, {running_request, PersistedScanState::Queued, false, {}, false});
        const int running_scan_id =
            persist_scan(db, repo, {running_request, PersistedScanState::Running, false, {}, false});

        all_ok = expect(queued_scan_id > 0, "queued fixture should persist") && all_ok;
        all_ok = expect(running_scan_id > 0, "running fixture should persist") && all_ok;

        ScanService service(db, logger);

        std::unique_ptr<PersistedScanSummary> queued_scan = service.get_scan(queued_scan_id);
        all_ok = expect(queued_scan != nullptr, "reconciled queued scan should remain readable") &&
                 all_ok;
        if (queued_scan)
        {
            all_ok = expect(queued_scan->state == PersistedScanState::Aborted,
                            "startup reconciliation should abort queued scans") &&
                     all_ok;
            all_ok =
                expect(queued_scan->message == "netscan terminated unexpectedly before scan completion",
                       "queued scan should record the stale shutdown message") &&
                all_ok;
        }

        std::unique_ptr<PersistedScanSummary> running_scan = service.get_scan(running_scan_id);
        all_ok =
            expect(running_scan != nullptr, "reconciled running scan should remain readable") &&
            all_ok;
        if (running_scan)
        {
            all_ok = expect(running_scan->state == PersistedScanState::Aborted,
                            "startup reconciliation should abort running scans") &&
                     all_ok;
        }
    }

    {
        const std::string cooldown_db_path =
            make_temp_db_path("/tmp/netscan-scan-cooldown-parse-test-XXXXXX.db");
        all_ok = expect(!cooldown_db_path.empty(),
                        "cooldown parse-failure database path should be created") &&
                 all_ok;

        const std::string cooldown_conf_temp_path =
            make_temp_db_path("/tmp/netscan-scan-cooldown-parse-test-XXXXXX.db");
        all_ok = expect(!cooldown_conf_temp_path.empty(),
                        "cooldown parse-failure config path should be created") &&
                 all_ok;
        if (!all_ok)
            return finish_test("scan_abort_service_test", false);
        const std::string cooldown_conf_path = cooldown_conf_temp_path + ".ini";
        std::remove(cooldown_conf_temp_path.c_str());

        {
            std::ofstream conf(cooldown_conf_path.c_str(), std::ios::out | std::ios::trunc);
            conf << "host=127.0.0.1\n";
            conf << "port=8080\n";
            conf << "db_path=./netscan.db\n";
            conf << "web_dir=./resources/web\n";
            conf << "log_level=info\n";
            conf << "scan_cooldown_seconds=60\n";
        }

        Database db(cooldown_db_path);
        Logger logger;
        configure_database_runtime(db, logger);
        init_schema(db, logger);
        ScanRepository repo(db);

        const ScanRequest cooldown_request{"127.0.0.1", "80", false, ""};
        const int completed_scan_id = persist_scan(
            db, repo, {cooldown_request, PersistedScanState::Completed, false, {}, false});
        all_ok = expect(completed_scan_id > 0,
                        "cooldown parse-failure fixture completed scan should persist") &&
                 all_ok;

        db.write([&](sqlite3* h) {
            sqlite3_stmt* stmt = nullptr;
            const int prepare_rc = sqlite3_prepare_v2(
                h, "UPDATE scans SET finished_at=? WHERE id=?;", -1, &stmt, nullptr);
            all_ok = expect(prepare_rc == SQLITE_OK,
                            "cooldown parse-failure fixture should prepare timestamp update") &&
                     all_ok;
            if (prepare_rc == SQLITE_OK)
            {
                sqlite3_bind_text(stmt, 1, "invalid timestamp", -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(stmt, 2, completed_scan_id);
                all_ok = expect(sqlite3_step(stmt) == SQLITE_DONE,
                                "cooldown parse-failure fixture should update finished_at") &&
                         all_ok;
            }
            sqlite3_finalize(stmt);
        });

        SettingsService settings(db, cooldown_conf_path, logger);
        ScanService service(db, logger, &settings);
        const ScanService::StartAsyncResult start_result =
            service.start_async({"127.0.0.1", "443", false, ""});
        all_ok = expect(start_result.status == ScanService::StartAsyncStatus::PolicyViolation,
                        "invalid finished_at should not bypass cooldown policy") &&
                 all_ok;
        all_ok = expect(start_result.error_message ==
                            "target was scanned too recently; retry in 60s",
                        "invalid finished_at should enforce full cooldown retry delay") &&
                 all_ok;

        std::remove(cooldown_conf_path.c_str());
        std::remove(cooldown_db_path.c_str());
    }

    std::remove(db_path.c_str());

#ifdef _WIN32
    return finish_test("scan_abort_service_test", all_ok);
#else
    const std::string runtime_db_path =
        make_temp_db_path("/tmp/netscan-scan-abort-runtime-test-XXXXXX.db");
    all_ok =
        expect(!runtime_db_path.empty(), "runtime temporary database path should be created") &&
        all_ok;
    if (!all_ok)
        return finish_test("scan_abort_service_test", false);

    {
        ScopedFakeNmap fake_nmap;
        all_ok = expect(fake_nmap.ready(), "fake nmap environment should be created") && all_ok;

        Database db(runtime_db_path);
        Logger logger;
        configure_database_runtime(db, logger);
        init_schema(db, logger);
        ScanService service(db, logger);

        ScanService::StartAsyncResult started =
            service.start_async({"127.0.0.1", "80", false, ""});
        all_ok = expect(started.status == ScanService::StartAsyncStatus::Accepted,
                        "start_async should accept a valid scan") &&
                 all_ok;
        all_ok = expect(service.terminate_scan(started.scan_id) ==
                            ScanService::TerminateScanStatus::Aborted,
                        "terminate_scan should abort an active scan before running is persisted") &&
                 all_ok;
        all_ok =
            expect(wait_for_scan_state(service, started.scan_id, PersistedScanState::Aborted, 5000),
                   "pre-running abort should persist the aborted state") &&
            all_ok;

        std::unique_ptr<PersistedScanSummary> pre_running_abort = service.get_scan(started.scan_id);
        all_ok = expect(pre_running_abort != nullptr, "pre-running aborted scan should be readable") &&
                 all_ok;
        if (pre_running_abort)
        {
            all_ok = expect(pre_running_abort->message == "terminated by user",
                            "pre-running abort should store the user abort message") &&
                     all_ok;
        }

        ScanService::StartAsyncResult running_started =
            service.start_async({"127.0.0.1", "81", false, ""});
        all_ok = expect(running_started.status == ScanService::StartAsyncStatus::Accepted,
                        "second scan should start after pre-running abort cleanup") &&
                 all_ok;
        all_ok =
            expect(wait_for_scan_state(service, running_started.scan_id,
                                       PersistedScanState::Running, 5000),
                   "scan should enter the running state before abort") &&
            all_ok;

        const ScanService::TerminateScanStatus abort_status =
            service.terminate_scan(running_started.scan_id);
        all_ok = expect(abort_status == ScanService::TerminateScanStatus::Aborted,
                        "terminate_scan should abort the running scan") &&
                 all_ok;
        all_ok =
            expect(wait_for_scan_state(service, running_started.scan_id,
                                       PersistedScanState::Aborted, 5000),
                   "aborted scan should persist the aborted state") &&
            all_ok;

        std::unique_ptr<PersistedScanSummary> aborted_scan = service.get_scan(running_started.scan_id);
        all_ok = expect(aborted_scan != nullptr, "aborted scan should remain readable") && all_ok;
        if (aborted_scan)
        {
            all_ok = expect(aborted_scan->message == "terminated by user",
                            "aborted scan should store the user abort message") &&
                     all_ok;
        }

        all_ok = expect(service.terminate_scan(running_started.scan_id) ==
                            ScanService::TerminateScanStatus::Conflict,
                        "terminate_scan should reject already finished scans") &&
                 all_ok;

        ScanService::StartAsyncResult mark_running_race =
            service.start_async({"127.0.0.1", "82", false, ""});
        all_ok = expect(mark_running_race.status == ScanService::StartAsyncStatus::Accepted,
                        "mark_running race scan should start") &&
                 all_ok;
        const ScanService::TerminateScanStatus mark_running_race_status =
            service.terminate_scan(mark_running_race.scan_id);
        all_ok =
            expect(mark_running_race_status == ScanService::TerminateScanStatus::Aborted,
                   "terminate_scan should abort while mark_scan_running races with worker start") &&
            all_ok;
        all_ok = expect(wait_for_scan_state(service, mark_running_race.scan_id,
                                            PersistedScanState::Aborted, 5000),
                        "mark_running race scan should persist aborted") &&
                 all_ok;

        ScanService::StartAsyncResult process_started_race =
            service.start_async({"127.0.0.1", "83", false, ""});
        all_ok = expect(process_started_race.status == ScanService::StartAsyncStatus::Accepted,
                        "process-started race scan should start") &&
                 all_ok;
        ScanService::TerminateScanStatus process_started_race_status =
            ScanService::TerminateScanStatus::Error;
        std::thread process_started_abort_thread([&]() {
            process_started_race_status = service.terminate_scan(process_started_race.scan_id);
        });
        service.on_nmap_process_started(process_started_race.scan_id, NmapProcessHandle());
        process_started_abort_thread.join();
        all_ok =
            expect(process_started_race_status == ScanService::TerminateScanStatus::Aborted,
                   "terminate_scan should abort when racing with on_nmap_process_started") &&
            all_ok;
        all_ok = expect(wait_for_scan_state(service, process_started_race.scan_id,
                                            PersistedScanState::Aborted, 5000),
                        "process-started race scan should persist aborted") &&
                 all_ok;

        ScanService::StartAsyncResult process_finished_race =
            service.start_async({"127.0.0.1", "84", false, ""});
        all_ok = expect(process_finished_race.status == ScanService::StartAsyncStatus::Accepted,
                        "process-finished race scan should start") &&
                 all_ok;
        ScanService::TerminateScanStatus process_finished_race_status =
            ScanService::TerminateScanStatus::Error;
        std::thread process_finished_abort_thread([&]() {
            process_finished_race_status = service.terminate_scan(process_finished_race.scan_id);
        });
        service.on_nmap_process_finished(process_finished_race.scan_id);
        process_finished_abort_thread.join();
        all_ok =
            expect(process_finished_race_status == ScanService::TerminateScanStatus::Aborted,
                   "terminate_scan should abort when racing with on_nmap_process_finished") &&
            all_ok;
        all_ok = expect(wait_for_scan_state(service, process_finished_race.scan_id,
                                            PersistedScanState::Aborted, 5000),
                        "process-finished race scan should persist aborted") &&
                 all_ok;

        ScanService::StartAsyncResult shutdown_scan =
            service.start_async({"127.0.0.1", "443", false, ""});
        all_ok = expect(shutdown_scan.status == ScanService::StartAsyncStatus::Accepted,
                        "second scan should start after user abort cleanup") &&
                 all_ok;
        all_ok = expect(wait_for_scan_state(service, shutdown_scan.scan_id,
                                            PersistedScanState::Running, 5000),
                        "second scan should enter running before shutdown cleanup") &&
                 all_ok;

        service.terminate_all_running_scans("terminated during shutdown");
        all_ok = expect(wait_for_scan_state(service, shutdown_scan.scan_id,
                                            PersistedScanState::Aborted, 5000),
                        "shutdown cleanup should abort the running scan") &&
                 all_ok;

        std::unique_ptr<PersistedScanSummary> shutdown_result =
            service.get_scan(shutdown_scan.scan_id);
        all_ok =
            expect(shutdown_result != nullptr, "shutdown-aborted scan should remain readable") &&
            all_ok;
        if (shutdown_result)
        {
            all_ok = expect(shutdown_result->message == "terminated during shutdown",
                            "shutdown cleanup should persist the shutdown message") &&
                     all_ok;
        }
    }

    std::remove(runtime_db_path.c_str());
    if (!all_ok)
        return finish_test("scan_abort_service_test", false);

    const std::string persist_failure_db_path =
        make_temp_db_path("/tmp/netscan-scan-persist-failure-test-XXXXXX.db");
    all_ok =
        expect(!persist_failure_db_path.empty(), "persist failure database path should be created") &&
        all_ok;
    if (!all_ok)
        return finish_test("scan_abort_service_test", false);

    {
        ScopedFakeNmap fake_nmap;
        all_ok =
            expect(fake_nmap.ready(),
                   "fake nmap environment should be created for persist failure") &&
            all_ok;

        Database db(persist_failure_db_path);
        Logger logger;
        configure_database_runtime(db, logger);
        init_schema(db, logger);
        ScanService service(db, logger);

        ScanService::StartAsyncResult started =
            service.start_async({"127.0.0.1", "80", false, ""});
        all_ok = expect(started.status == ScanService::StartAsyncStatus::Accepted,
                        "persist failure scan should start") &&
                 all_ok;
        all_ok =
            expect(wait_for_scan_state(service, started.scan_id, PersistedScanState::Running, 5000),
                   "persist failure scan should enter running") &&
            all_ok;

        db.write([&](sqlite3* h) {
            sqlite3_stmt* stmt = nullptr;
            const int prepare_rc =
                sqlite3_prepare_v2(h, "DELETE FROM scans WHERE id=?;", -1, &stmt, nullptr);
            all_ok = expect(prepare_rc == SQLITE_OK,
                            "persist failure fixture should prepare scan deletion") &&
                     all_ok;
            if (prepare_rc == SQLITE_OK)
            {
                sqlite3_bind_int(stmt, 1, started.scan_id);
                all_ok = expect(sqlite3_step(stmt) == SQLITE_DONE,
                                "persist failure fixture should delete the active scan row") &&
                         all_ok;
            }
            sqlite3_finalize(stmt);
        });

        service.terminate_all_running_scans("forced persist failure");

        std::unique_ptr<PersistedScanSummary> status = service.get_status();
        all_ok = expect(status != nullptr,
                        "service should expose a transient terminal status after persist failure") &&
                 all_ok;
        if (status)
        {
            all_ok = expect(status->id == started.scan_id,
                            "transient status should belong to the failed scan") &&
                     all_ok;
            all_ok = expect(status->state == PersistedScanState::Aborted,
                            "transient status should preserve the terminal abort state") &&
                     all_ok;
            all_ok = expect(status->message == "forced persist failure",
                            "transient status should preserve the shutdown reason") &&
                     all_ok;
        }

        std::unique_ptr<PersistedScanSummary> scan = service.get_scan(started.scan_id);
        all_ok = expect(scan != nullptr,
                        "get_scan should return the transient terminal status after persist failure") &&
                 all_ok;
        if (scan)
        {
            all_ok = expect(scan->state == PersistedScanState::Aborted,
                            "get_scan should not fall back to a stale running DB state") &&
                     all_ok;
        }

        all_ok = expect(service.terminate_scan(started.scan_id) ==
                            ScanService::TerminateScanStatus::Conflict,
                        "terminate_scan should treat the transient terminal scan as already finished") &&
                 all_ok;
    }

    std::remove(persist_failure_db_path.c_str());

    const std::string chunk_policy_db_path =
        make_temp_db_path("/tmp/netscan-scan-chunk-policy-test-XXXXXX.db");
    all_ok = expect(!chunk_policy_db_path.empty(),
                    "chunk policy database path should be created") &&
             all_ok;
    if (!all_ok)
        return finish_test("scan_abort_service_test", false);

    {
        ScopedFakeNmap fake_nmap("/tmp/netscan-fake-chunk-policy-nmap-XXXXXX",
                                 "<nmaprun></nmaprun>", true);
        all_ok = expect(fake_nmap.ready(),
                        "chunk policy fake nmap environment should be created") &&
                 all_ok;

        Database db(chunk_policy_db_path);
        Logger logger;
        configure_database_runtime(db, logger);
        init_schema(db, logger);
        ScanService service(db, logger);

        const ScanService::StartAsyncResult first_chunked =
            service.start_async({"192.168.10.0/26", "22", false, ""});
        all_ok = expect(first_chunked.status == ScanService::StartAsyncStatus::Accepted,
                        "first chunked scan should start") &&
                 all_ok;

        const ScanService::StartAsyncResult same_target =
            service.start_async({"192.168.10.0/26", "80", false, ""});
        all_ok = expect(same_target.status == ScanService::StartAsyncStatus::Conflict,
                        "same target should remain a conflict before chunk policy") &&
                 all_ok;

        const ScanService::StartAsyncResult second_chunked =
            service.start_async({"192.168.20.0/26", "22", false, ""});
        all_ok = expect(second_chunked.status == ScanService::StartAsyncStatus::PolicyViolation,
                        "second chunked scan should be rejected at the global limit") &&
                 all_ok;

        const ScanService::StartAsyncResult non_chunked =
            service.start_async({"127.0.0.1", "22", false, ""});
        all_ok = expect(non_chunked.status == ScanService::StartAsyncStatus::Accepted,
                        "non-chunked scan should not be blocked by chunked scan limit") &&
                 all_ok;

        service.terminate_all_running_scans("terminated during chunk policy test");
        all_ok = expect(wait_for_scan_state(service, first_chunked.scan_id,
                                            PersistedScanState::Aborted, 5000),
                        "chunk policy fixture should clean up first chunked scan") &&
                 all_ok;
        all_ok = expect(wait_for_scan_state(service, non_chunked.scan_id,
                                            PersistedScanState::Aborted, 5000),
                        "chunk policy fixture should clean up non-chunked scan") &&
                 all_ok;
    }

    std::remove(chunk_policy_db_path.c_str());

    const std::string chunk_size_db_path =
        make_temp_db_path("/tmp/netscan-scan-chunk-size-test-XXXXXX.db");
    all_ok = expect(!chunk_size_db_path.empty(),
                    "chunk size database path should be created") &&
             all_ok;
    if (!all_ok)
        return finish_test("scan_abort_service_test", false);

    {
        const std::string chunk_xml =
            "<nmaprun><host><status state=\"up\"/>"
            "<address addr=\"192.168.1.10\" addrtype=\"ipv4\"/></host></nmaprun>";
        ScopedFakeNmap fake_nmap("/tmp/netscan-fake-chunk-size-nmap-XXXXXX", chunk_xml, false);
        all_ok = expect(fake_nmap.ready(),
                        "chunk size fake nmap environment should be created") &&
                 all_ok;

        Database db(chunk_size_db_path);
        Logger logger;
        configure_database_runtime(db, logger);
        init_schema(db, logger);
        ScanService service(db, logger);

        const ScanService::StartAsyncResult too_large =
            service.start_async({"10.0.0.0/16", "22", false, ""});
        all_ok = expect(too_large.status == ScanService::StartAsyncStatus::PolicyViolation,
                        "oversized chunked port scan should be rejected") &&
                 all_ok;
        all_ok = expect(too_large.error_message == "scan target too large for chunked port scan",
                        "oversized chunked port scan should return a clear policy message") &&
                 all_ok;
        all_ok = expect(count_scan_rows(db) == 0,
                        "oversized chunked port scan should not create a queued scan row") &&
                 all_ok;

        const ScanService::StartAsyncResult normal =
            service.start_async({"192.168.30.0/24", "22", false, ""});
        all_ok = expect(normal.status == ScanService::StartAsyncStatus::Accepted,
                        "normal /24 chunked port scan should remain allowed") &&
                 all_ok;
        all_ok = expect(wait_for_scan_state(service, normal.scan_id,
                                            PersistedScanState::Completed, 5000),
                        "normal /24 chunked port scan should complete") &&
                 all_ok;
    }

    std::remove(chunk_size_db_path.c_str());

    const std::string chunked_db_path =
        make_temp_db_path("/tmp/netscan-scan-chunked-service-test-XXXXXX.db");
    all_ok = expect(!chunked_db_path.empty(),
                    "chunked scan temporary database path should be created") &&
             all_ok;
    if (!all_ok)
        return finish_test("scan_abort_service_test", false);

    {
        const std::string chunk_xml =
            "<nmaprun><host><status state=\"up\"/>"
            "<address addr=\"192.168.1.10\" addrtype=\"ipv4\"/>"
            "<ports><port protocol=\"tcp\" portid=\"22\">"
            "<state state=\"open\"/><service name=\"ssh\"/>"
            "</port></ports></host></nmaprun>";
        ScopedFakeNmap fake_nmap("/tmp/netscan-fake-chunked-nmap-XXXXXX", chunk_xml, false);
        all_ok = expect(fake_nmap.ready(),
                        "finishing fake nmap environment should be created") &&
                 all_ok;

        Database db(chunked_db_path);
        Logger logger;
        configure_database_runtime(db, logger);
        init_schema(db, logger);
        ScanService service(db, logger);

        const ScanService::StartAsyncResult started =
            service.start_async({"192.168.1.0/26", "22", false, ""});
        all_ok = expect(started.status == ScanService::StartAsyncStatus::Accepted,
                        "large IPv4 port scan should start") &&
                 all_ok;
        all_ok =
            expect(wait_for_scan_state(service, started.scan_id,
                                       PersistedScanState::Completed, 5000),
                   "large IPv4 port scan should complete through chunked path") &&
            all_ok;

        std::unique_ptr<PersistedScanSummary> scan = service.get_scan(started.scan_id);
        all_ok = expect(scan != nullptr, "chunked completed scan should be readable") && all_ok;
        if (scan)
        {
            all_ok = expect(scan->command.find("chunked nmap scan") != std::string::npos,
                            "chunked completed scan should record chunked command summary") &&
                     all_ok;
        }

        ScanRepository repo(db);
        db.read([&](sqlite3* h) {
            const std::vector<ScanHostRow> hosts = repo.list_scan_hosts(h, started.scan_id);
            const std::vector<ScanPortRow> ports = repo.list_scan_ports(h, started.scan_id);
            all_ok = expect(hosts.size() == 1,
                            "chunked completed scan should persist merged host snapshot") &&
                     all_ok;
            all_ok = expect(ports.size() == 1,
                            "chunked completed scan should persist merged port snapshot") &&
                     all_ok;
        });
    }

    std::remove(chunked_db_path.c_str());
    return finish_test("scan_abort_service_test", all_ok);
#endif
}
