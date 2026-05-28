#include "db/database.hpp"
#include "db/schema.hpp"
#include "db/scan_repository.hpp"
#include "scan_service_test_support.hpp"
#include "service/host_service.hpp"
#include "test_output.hpp"
#include "util/logger.hpp"

#include <cstdio>
#include <memory>
#include <string>

int main()
{
    bool all_ok = true;

    const std::string db_path = make_temp_db_path("/tmp/netscan-host-service-detail-test-XXXXXX.db");
    all_ok = expect(!db_path.empty(), "temporary database path should be created") && all_ok;

    if (!db_path.empty())
    {
        Database db(db_path);
        Logger logger;
        configure_database_runtime(db, logger);
        init_schema(db, logger);

        ScanRepository repo(db);
        HostService host_service(db);

        const std::string host_ip = "192.168.1.20";
        const std::string host_name = "printer.local";

        const int first_scan_id = persist_scan(
            db, repo,
            {{host_ip, "", false, ""}, PersistedScanState::Completed, false,
             lsm::scan::ScanSnapshot{{{host_ip, host_name, {}}}}});
        all_ok = expect(first_scan_id > 0, "first scan should persist") && all_ok;

        const int second_scan_id = persist_scan(
            db, repo,
            {{host_ip, "", false, ""}, PersistedScanState::Completed, false,
             lsm::scan::ScanSnapshot{{{host_ip, host_name,
                                       {{"tcp", 9100, "open", "jetdirect"},
                                        {"tcp", 631, "open", "ipp"}}}}}});
        all_ok = expect(second_scan_id > 0, "second scan should persist") && all_ok;

        const int third_scan_id = persist_scan(
            db, repo,
            {{host_ip, "", false, ""}, PersistedScanState::Completed, false,
             lsm::scan::ScanSnapshot{{{host_ip, host_name,
                                       {{"tcp", 80, "open", "http"},
                                        {"tcp", 443, "open", "https"}}}}}});
        all_ok = expect(third_scan_id > 0, "third scan should persist") && all_ok;

        std::unique_ptr<HostDetail> detail = host_service.get_host_detail(host_ip);
        all_ok = expect(detail != nullptr, "host detail should exist for a persisted host") && all_ok;

        if (detail)
        {
            all_ok = expect(detail->host.ip == host_ip, "host detail should preserve the IP") &&
                     all_ok;
            all_ok =
                expect(detail->host.name == host_name, "host detail should preserve the name") &&
                all_ok;
            all_ok = expect(detail->history.size() == 3,
                            "host detail should include all persisted scans for the host") &&
                     all_ok;

            if (detail->history.size() == 3)
            {
                all_ok = expect(detail->history[0].scan.id == third_scan_id,
                                "history should be ordered by descending scan id") &&
                         all_ok;
                all_ok = expect(detail->history[1].scan.id == second_scan_id,
                                "history should include the older scan second") &&
                         all_ok;
                all_ok = expect(detail->history[2].scan.id == first_scan_id,
                                "history should include the oldest scan last") &&
                         all_ok;

                all_ok = expect(detail->history[0].ports.size() == 2,
                                "latest scan should include both persisted ports") &&
                         all_ok;
                if (detail->history[0].ports.size() == 2)
                {
                    all_ok = expect(detail->history[0].ports[0].port == 80,
                                    "latest scan ports should remain sorted ascending") &&
                             all_ok;
                    all_ok = expect(detail->history[0].ports[0].service == "http",
                                    "latest scan should preserve the first port service") &&
                             all_ok;
                    all_ok = expect(detail->history[0].ports[1].port == 443,
                                    "latest scan should preserve the second port number") &&
                             all_ok;
                    all_ok = expect(detail->history[0].ports[1].service == "https",
                                    "latest scan should preserve the second port service") &&
                             all_ok;
                }

                all_ok = expect(detail->history[1].ports.size() == 2,
                                "older scan should include its own ports") &&
                         all_ok;
                if (detail->history[1].ports.size() == 2)
                {
                    all_ok = expect(detail->history[1].ports[0].port == 631,
                                    "older scan ports should remain sorted ascending") &&
                             all_ok;
                    all_ok = expect(detail->history[1].ports[0].service == "ipp",
                                    "older scan should preserve the first port service") &&
                             all_ok;
                    all_ok = expect(detail->history[1].ports[1].port == 9100,
                                    "older scan should preserve the second port number") &&
                             all_ok;
                    all_ok = expect(detail->history[1].ports[1].service == "jetdirect",
                                    "older scan should preserve the second port service") &&
                             all_ok;
                }

                all_ok = expect(detail->history[2].ports.empty(),
                                "oldest scan should remain in history with no open ports") &&
                         all_ok;
            }
        }

        all_ok = expect(host_service.get_host_detail("192.168.1.99") == nullptr,
                        "missing hosts should still return null") &&
                 all_ok;
    }

    if (!db_path.empty())
        std::remove(db_path.c_str());

    return finish_test("host_service_detail_test", all_ok);
}
