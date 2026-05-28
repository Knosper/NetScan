#ifndef SERVICE_SCAN_SERVICE_HPP
#define SERVICE_SCAN_SERVICE_HPP

#include "db/database.hpp"
#include "db/scan_repository.hpp"
#include "db/write_transaction.hpp"
#include "scan/nmap_runner.hpp"
#include "scan/scan_chunk_planner.hpp"
#include "scan/scan_types.hpp"
#include "service/i_scan_starter.hpp"
#include "service/scan_completion.hpp"
#include "service/scan_runtime.hpp"
#include "util/logger.hpp"

class SettingsService;
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class ScanService : public IScanStarter
{
public:
    using IScanStarter::StartAsyncStatus;
    using IScanStarter::StartAsyncResult;

    ScanService(Database& db, Logger& logger,
                SettingsService* settings = nullptr);
    ~ScanService();

    void set_nmap_path(const std::string& path);

    // Starts an asynchronous scan.
    // REQUIRES: caller obeys database locking rules enforced internally.
    StartAsyncResult start_async(const ScanRequest& request) override;

    // Returns currently running scan if any, otherwise the latest persisted scan.
    // REQUIRES: caller obeys database locking rules enforced internally.
    std::unique_ptr<PersistedScanSummary> get_status();

    struct StatusView
    {
        std::unique_ptr<PersistedScanSummary> scan;
        int progress = -1;
        int eta_seconds = -1;
        int hosts_found = -1;
    };

    StatusView get_status_view();

    // Returns scan progress (0-100). Returns -1 when the scan has no active progress.
    int get_progress(int scan_id);

    // Returns the ETA in seconds remaining. Returns -1 when not available.
    int get_eta(int scan_id);

    // Returns the live hosts-found count. Returns -1 when not available.
    int get_hosts_found(int scan_id);

    std::unique_ptr<PersistedScanSummary> get_scan(int scan_id);

    // Returns host summaries (ip, hostname, open port count) for a given scan.
    // Returns an empty vector when the scan has no persisted hosts yet.
    std::vector<ScanHostSummary> get_scan_host_summaries(int scan_id);

    enum class DeleteScanStatus
    {
        Deleted,
        NotFound,
        Conflict,
        Error
    };

    enum class TerminateScanStatus
    {
        Aborted,
        NotFound,
        Conflict,
        Error
    };

    DeleteScanStatus delete_scan(int scan_id);
    TerminateScanStatus terminate_scan(int scan_id);
    void terminate_all_running_scans(const std::string& reason);
    void on_nmap_process_started(int scan_id, const NmapProcessHandle& handle);
    void on_nmap_process_finished(int scan_id);
    void on_nmap_process_finished(int scan_id, const NmapProcessHandle& handle);
    void on_nmap_progress(int scan_id, int percent);
    void on_nmap_command_ready(int scan_id, const std::string& cmd);
    void on_nmap_eta(int scan_id, int seconds);
    void on_nmap_hosts_found(int scan_id, int count);

private:
    struct RequestValidationResult
    {
        bool        ok = false;
        std::string error_message;
        std::string normalized_ports;
    };

    struct ScanExecutionResult
    {
        ScanCompletionInput input;
        bool chunked = false;
        ScanChunkRunResult chunk_result;
    };

    Database&        db_;
    Logger&          logger_;
    ScanRepository   repo_;
    SettingsService* settings_;
    std::string      nmap_path_;
    struct ScanWorker
    {
        std::thread                            thread;
        std::shared_ptr<std::atomic<bool>>     finished_flag;
    };

    std::mutex                 start_mutex_;
    std::mutex                 workers_mutex_;
    std::vector<ScanWorker>    workers_;
    ScanRuntime                runtime_;
    static const std::chrono::milliseconds abort_wait_timeout_;

    RequestValidationResult validate_request(const ScanRequest& request) const;
    StartAsyncResult check_cooldown(const std::string& target, int cooldown_seconds);
    StartAsyncResult check_cooldown_policy(const ScanRequest& normalized_request);
    StartAsyncResult check_dependencies() const;
    StartAsyncResult check_configured_nmap_path() const;
    StartAsyncResult check_chunk_scan_policies(const ScanChunkPlan& chunk_plan) const;
    StartAsyncResult launch_worker_thread(int scan_id, const ScanRequest& normalized_request);
    StartAsyncResult dispatch_scan_worker(const ScanRequest& normalized_request);
    DeleteScanStatus validate_scan_deletable(const std::unique_ptr<PersistedScanSummary>& scan,
                                              int scan_id);
    DeleteScanStatus perform_soft_delete(sqlite3* h, WriteTransaction& tx,
                                         int scan_id, const std::string& target);
    TerminateScanStatus resolve_inactive_scan_status(int scan_id);
    bool terminate_abort_handles(int scan_id, const ScanRuntime::AbortContext& ctx);
    void worker_thread(int scan_id, ScanRequest request,
                       std::shared_ptr<std::atomic<bool>> finished_flag);
    void worker_thread_body(int scan_id, const ScanRequest& request);
    ScanExecutionResult make_pre_abort_result(int scan_id, const ScanRequest& request);
    void execute_chunked_scan(int scan_id, const ScanChunkPlan& chunk_plan,
                              ScanExecutionResult& execution);
    void apply_post_run_abort_override(int scan_id, ScanExecutionResult& execution);
    ScanExecutionResult execute_scan_with_abort_check(int scan_id, const ScanRequest& request);
    void finalize_scan_completion(const ScanExecutionResult& execution);
    void reap_finished_workers();
    void reconcile_incomplete_scans();
};

#endif
