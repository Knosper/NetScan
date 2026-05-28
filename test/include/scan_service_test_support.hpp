#ifndef SCAN_SERVICE_TEST_SUPPORT_HPP
#define SCAN_SERVICE_TEST_SUPPORT_HPP

#include "db/database.hpp"
#include "db/scan_repository.hpp"
#include "httplib/httplib.h"
#include "scan/scan_snapshot.hpp"
#include "service/scan_service.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

struct ServiceTestScanFixture
{
    ScanRequest              request;
    PersistedScanState       state = PersistedScanState::Completed;
    bool                     host_discovery_only = false;
    lsm::scan::ScanSnapshot  snapshot;
    bool                     deleted = false;
};

struct TestRepoPaths
{
    std::string repo_root;
    std::string web_dir;
    std::string conf_path;
};

bool expect(bool condition, const std::string& message);
bool expect_response_status(const httplib::Result& result, int status, const std::string& message);
std::string make_temp_db_path(const char* path_template);
TestRepoPaths make_test_repo_paths(const char* test_file_path);
void configure_test_client(httplib::Client& client, int read_timeout_seconds = 2);
int count_ports_for_host(Database& db, int host_id);
int persist_scan(Database& db, ScanRepository& repo, const ServiceTestScanFixture& fixture);

class ScopedFakeNmap
{
public:
    explicit ScopedFakeNmap(const std::string& directory_template = "/tmp/netscan-fake-nmap-XXXXXX",
                            const std::string& output_line = "fake nmap started",
                            bool keep_running = true);
    ~ScopedFakeNmap();

    bool ready() const;
    std::string executable_path() const;

private:
    std::string dir_;
    std::string script_path_;
    std::string original_path_;
    bool        ready_ = false;
};

inline bool is_terminal_state(PersistedScanState state)
{
    return state == PersistedScanState::Completed || state == PersistedScanState::Failed ||
           state == PersistedScanState::DependencyMissing ||
           state == PersistedScanState::Aborted;
}

inline bool wait_for_scan_state(ScanService& service, int scan_id,
                                PersistedScanState expected_state, int timeout_ms)
{
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < deadline)
    {
        std::unique_ptr<PersistedScanSummary> scan = service.get_scan(scan_id);
        if (scan && scan->state == expected_state)
            return true;

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    return false;
}

inline bool wait_for_terminal_scan(ScanService& service, int scan_id, int timeout_ms)
{
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < deadline)
    {
        std::unique_ptr<PersistedScanSummary> scan = service.get_scan(scan_id);
        if (scan && is_terminal_state(scan->state))
            return true;

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    return false;
}

#endif
