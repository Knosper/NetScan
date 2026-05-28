#include "service/scan_service.hpp"
#include "scan/port_spec_validator.hpp"
#include "scan/target_validation.hpp"
#include "service/settings_service.hpp"

#include "db/sqlite_helpers.hpp"
#include "db/write_transaction.hpp"
#include "scan/nmap.hpp"
#include "scan/nmap_xml_parser.hpp"
#include "scan/scan_chunk_planner.hpp"
#include "scan/scan_chunk_runner.hpp"
#include "scan/scan_orchestrator.hpp"
#include "service/scan_completion.hpp"
#include "service/scan_messages.hpp"
#include "service/scan_result_builders.hpp"
#include "service/scan_persistence.hpp"
#include "util/logger.hpp"
#include "util/time_utils.hpp"

#include <atomic>
#include <chrono>
#include <ctime>
#include <exception>
#include <fstream>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace
{
constexpr std::size_t MAX_ACTIVE_CHUNKED_SCANS = 1;

struct ScanStorage
{
    Database& db;
    ScanRepository& repo;
    Logger& logger;
};


std::string now_utc_timestamp()
{
    return util::format_utc_time(std::time(nullptr), "%Y-%m-%d %H:%M:%S");
}

PersistedScanSummary make_transient_summary(const ScanCompletionInput& input,
                                            const ScanCompletionResult& completion)
{
    PersistedScanSummary scan;
    scan.id = input.scan_id;
    scan.target = input.request.target;
    scan.host_discovery_only = input.request.host_discovery_only;
    scan.requested_ports = input.request.ports;
    scan.state = completion.final_state;
    scan.message = completion.final_message;
    scan.command = input.result.command;
    scan.stderr_text = input.result.stderr_text;
    if (input.result.exit_code >= 0)
    {
        scan.exit_code = input.result.exit_code;
        scan.has_exit_code = true;
    }
    scan.finished_at = input.finished_at;
    scan.deleted = false;
    return scan;
}

int insert_queued_scan(const ScanStorage& storage, const ScanRequest& request)
{
    return storage.db.write([&storage, &request](sqlite3* h)
                            { return storage.repo.insert_scan_queued(h, request); });
}

void persist_scan_failure(const ScanStorage& storage, int scan_id, const char* message)
{
    storage.db.write(
        [&storage, scan_id, message](sqlite3* h)
        {
            if (!storage.repo.mark_scan_failed(
                    h, scan_id, failed_scan_result(message), now_utc_timestamp()))
            {
                storage.logger.error("failed to persist scan failure state");
            }
        });
}

void force_terminate_handles_for_shutdown(
    const std::vector<std::pair<int, NmapProcessHandle>>& handles, Logger& logger)
{
    for (std::vector<std::pair<int, NmapProcessHandle>>::const_iterator it = handles.begin();
         it != handles.end(); ++it)
    {
        std::string error;
        if (!force_terminate_nmap_process(it->second, &error))
        {
            logger.error("failed to force-terminate running scan during shutdown: scan id " +
                         std::to_string(it->first) + ": " + error);
        }
    }
}

bool mark_scan_running(const ScanStorage& storage, int scan_id)
{
    const std::string now = now_utc_timestamp();
    return storage.db.write(
        [&storage, scan_id, &now](sqlite3* h)
        {
            if (storage.repo.mark_scan_running(h, scan_id, now))
                return true;

            storage.logger.error("Failed to mark scan running");
            if (!storage.repo.mark_scan_failed(
                    h, scan_id,
                    failed_scan_result(lsm::service::scan_messages::FAILED_TO_MARK_RUNNING),
                    now))
            {
                storage.logger.error(
                    "failed to persist scan failure state after mark_running error");
            }
            return false;
        });
}

ScanResult run_scan_safely(const ScanRequest& request, Logger& logger,
                           NmapProcessListener* listener)
{
    try
    {
        ScanOrchestrator orchestrator(logger, listener);
        return orchestrator.run_sync(request);
    }
    catch (const std::exception& e)
    {
        logger.error(std::string("scan worker exception: ") + e.what());
        return failed_scan_result("unexpected error");
    }
    catch (...)
    {
        logger.error("scan worker exception: unknown exception");
        return failed_scan_result("unexpected error");
    }
}

std::string chunked_command_summary(std::size_t chunk_count)
{
    return "chunked nmap scan (" + std::to_string(chunk_count) + " chunks)";
}

std::string chunked_command_progress_summary(std::size_t completed, std::size_t total)
{
    return "chunked nmap scan (" + std::to_string(completed) + "/" +
           std::to_string(total) + " chunks complete)";
}

class ScanWorkerListener : public NmapProcessListener
{
public:
    ScanWorkerListener(ScanService& service, int scan_id)
        : service_(service), scan_id_(scan_id)
    {
    }

    void on_nmap_process_started(const NmapProcessHandle& handle) override
    {
        service_.on_nmap_process_started(scan_id_, handle);
    }

    void on_nmap_process_finished() override
    {
        service_.on_nmap_process_finished(scan_id_);
    }

    void on_nmap_progress(int percent) override
    {
        service_.on_nmap_progress(scan_id_, percent);
    }

    void on_nmap_command_ready(const std::string& cmd) override
    {
        service_.on_nmap_command_ready(scan_id_, cmd);
    }

    void on_nmap_eta(int seconds) override
    {
        service_.on_nmap_eta(scan_id_, seconds);
    }

    void on_nmap_hosts_found(int count) override
    {
        service_.on_nmap_hosts_found(scan_id_, count);
    }

private:
    ScanService& service_;
    int          scan_id_;
};

class ChunkWorkerListener : public NmapProcessListener
{
public:
    ChunkWorkerListener(ScanService& service, int scan_id,
                        const std::function<void(int)>& progress_callback)
        : service_(service), scan_id_(scan_id), progress_callback_(progress_callback)
    {
    }

    void on_nmap_process_started(const NmapProcessHandle& handle) override
    {
        handle_ = handle;
        has_handle_ = true;
        service_.on_nmap_process_started(scan_id_, handle);
    }

    void on_nmap_process_finished() override
    {
        if (has_handle_)
            service_.on_nmap_process_finished(scan_id_, handle_);
    }

    void on_nmap_progress(int percent) override
    {
        if (progress_callback_)
            progress_callback_(percent);
    }

private:
    ScanService& service_;
    int          scan_id_;
    std::function<void(int)> progress_callback_;
    NmapProcessHandle handle_;
    bool has_handle_ = false;
};

static int seconds_since_utc_timestamp(const std::string& ts)
{
    if (ts.empty())
        return -1;

    std::tm tm{};
    if (std::sscanf(ts.c_str(), "%d-%d-%d %d:%d:%d",
                    &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                    &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6)
        return -1;

    // Reject out-of-range fields — timegm normalises them silently into far-future timestamps
    if (tm.tm_mon  < 1  || tm.tm_mon  > 12) return -1;
    if (tm.tm_mday < 1  || tm.tm_mday > 31) return -1;
    if (tm.tm_hour < 0  || tm.tm_hour > 23) return -1;
    if (tm.tm_min  < 0  || tm.tm_min  > 59) return -1;
    if (tm.tm_sec  < 0  || tm.tm_sec  > 60) return -1;

    tm.tm_year -= 1900;
    tm.tm_mon  -= 1;
    tm.tm_isdst = 0;

#if defined(_WIN32)
    const std::time_t then = _mkgmtime(&tm);
#else
    const std::time_t then = timegm(&tm);
#endif
    if (then == static_cast<std::time_t>(-1))
        return -1;

    using namespace std::chrono;
    const std::time_t now = system_clock::to_time_t(system_clock::now());
    const double diff = std::difftime(now, then);
    return diff >= 0 ? static_cast<int>(diff) : 0;
}

bool file_exists(const std::string& path)
{
    if (path.empty())
        return false;

    std::ifstream file(path.c_str(), std::ios::in | std::ios::binary);
    return static_cast<bool>(file);
}

} // namespace

const std::chrono::milliseconds ScanService::abort_wait_timeout_(5000);

ScanService::ScanService(Database& db, Logger& logger, SettingsService* settings)
    : db_(db), logger_(logger), repo_(db), settings_(settings)
{
    reconcile_incomplete_scans();
}

void ScanService::set_nmap_path(const std::string& path)
{
    nmap_path_ = path;
}

ScanService::~ScanService()
{
    // terminate_all_running_scans is also called from shutdown_services before the server
    // stops. The second call here is a safe no-op: shutdown_services first stops the
    // scheduler (no new scans can be dispatched) and then calls terminate_all_running_scans,
    // which waits until active_scans_ is empty before returning. By the time this destructor
    // runs, active_scans_ is guaranteed empty and has_active_scans() returns false
    // immediately, so the function returns on the guard clause without entering wait_all_stopped.
    terminate_all_running_scans("terminated during shutdown");

    std::vector<ScanWorker> workers;
    {
        std::lock_guard<std::mutex> lock(workers_mutex_);
        workers.swap(workers_);
    }

    for (auto it = workers.begin(); it != workers.end(); ++it)
    {
        if (it->thread.joinable())
            it->thread.join();
    }
}

void ScanService::reconcile_incomplete_scans()
{
    const int updated = db_.write(
        [this](sqlite3* h)
        {
            return repo_.abort_incomplete_scans(
                h, lsm::service::scan_messages::SCAN_ABORTED_STALE_STARTUP, now_utc_timestamp());
        });
    if (updated < 0)
    {
        logger_.error("failed to reconcile incomplete scans on startup");
        return;
    }

    if (updated > 0)
    {
        logger_.warn("reconciled " + std::to_string(updated) +
                     " incomplete scan(s) left from a previous shutdown");
    }
}

int ScanService::get_progress(int scan_id)
{
    return runtime_.get_progress(scan_id);
}

ScanService::RequestValidationResult
ScanService::validate_request(const ScanRequest& request) const
{
    if (request.target.empty())
        return {false, "field 'target' must not be empty", ""};

    if (!is_valid_scan_target(request.target))
        return {false, "invalid target format", ""};

    if (request.host_discovery_only && !request.ports.empty())
        return {false, "field 'ports' must be omitted when 'host_discovery_only' is true", ""};

    const PortSpecValidation port_validation = validate_port_spec(request.ports);
    if (!port_validation.ok)
        return {false, port_validation.error_message, ""};

    return {true, "", port_validation.validated_spec};
}

ScanService::StartAsyncResult ScanService::check_cooldown(const std::string& target,
                                                          int cooldown_seconds)
{
    if (cooldown_seconds <= 0)
        return {StartAsyncStatus::Accepted, -1, ""};

    std::unique_ptr<PersistedScanSummary> last =
        db_.read([this, &target](sqlite3* h)
                 { return repo_.get_last_completed_scan_for_target(h, target); });

    if (!last)
        return {StartAsyncStatus::Accepted, -1, ""};

    const int elapsed = seconds_since_utc_timestamp(last->finished_at);
    if (elapsed >= cooldown_seconds)
        return {StartAsyncStatus::Accepted, -1, ""};

    const int remaining = elapsed < 0 ? cooldown_seconds : cooldown_seconds - elapsed;
    return {StartAsyncStatus::PolicyViolation, -1,
            "target was scanned too recently; retry in " + std::to_string(remaining) + "s"};
}

std::unique_ptr<PersistedScanSummary> ScanService::get_status()
{
    return get_status_view().scan;
}

ScanService::StatusView ScanService::get_status_view()
{
    reap_finished_workers();

    ScanService::StatusView view;
    auto snap = runtime_.get_status_snapshot();
    const int active_id = snap.active_id;
    const bool has_active = snap.has_active;

    view.scan = db_.read(
        [this, has_active, active_id, tl = std::move(snap.transient)](sqlite3* h) mutable
        {
            if (has_active && active_id > 0)
                return repo_.get_scan_by_id(h, active_id);

            std::unique_ptr<PersistedScanSummary> persisted = repo_.get_latest_scan(h);
            if (!tl)
                return persisted;
            if (!persisted || tl->id >= persisted->id)
                return std::move(tl);
            return persisted;
        });

    if (view.scan && has_active && view.scan->id == active_id)
    {
        if (!snap.command.empty())
            view.scan->command = snap.command;
        view.progress = snap.progress;
        view.eta_seconds = snap.eta_seconds;
        view.hosts_found = snap.hosts_found;
    }

    return view;
}

std::unique_ptr<PersistedScanSummary> ScanService::get_scan(int scan_id)
{
    auto transient = runtime_.get_transient_terminal(scan_id);
    if (transient)
        return transient;

    return db_.read([this, scan_id](sqlite3* h)
                    {
                        auto scan = repo_.get_scan_by_id(h, scan_id);
                        if (!scan || scan->deleted)
                            return std::unique_ptr<PersistedScanSummary>();
                        return scan;
                    });
}

std::vector<ScanHostSummary> ScanService::get_scan_host_summaries(int scan_id)
{
    return db_.read(
        [this, scan_id](sqlite3* h)
        {
            auto hosts = repo_.list_scan_hosts(h, scan_id);
            auto ports = repo_.list_scan_ports(h, scan_id);

            std::unordered_map<std::string, int> port_counts;
            for (const auto& p : ports)
            {
                if (p.state == "open")
                    port_counts[p.host_ip]++;
            }

            std::vector<ScanHostSummary> result;
            result.reserve(hosts.size());
            for (const auto& h : hosts)
            {
                ScanHostSummary s;
                s.ip = h.ip;
                s.hostname = h.name;
                auto it = port_counts.find(h.ip);
                s.open_port_count = (it != port_counts.end()) ? it->second : 0;
                result.push_back(s);
            }
            return result;
        });
}

void ScanService::on_nmap_process_started(int scan_id, const NmapProcessHandle& handle)
{
    const bool should_terminate = runtime_.register_process(scan_id, handle);
    if (!should_terminate)
        return;

    std::string error;
    if (!terminate_nmap_process(handle, &error))
        logger_.error("failed to terminate nmap process after abort request: " + error);
}

void ScanService::on_nmap_progress(int scan_id, int percent)
{
    runtime_.update_progress(scan_id, clamp_progress_percent(percent));
}

void ScanService::on_nmap_process_finished(int scan_id)
{
    runtime_.unregister_process(scan_id);
}

void ScanService::on_nmap_process_finished(int scan_id, const NmapProcessHandle& handle)
{
    runtime_.unregister_process(scan_id, handle);
}

void ScanService::on_nmap_command_ready(int scan_id, const std::string& cmd)
{
    runtime_.update_command(scan_id, cmd);
}

int ScanService::get_eta(int scan_id)
{
    return runtime_.get_eta(scan_id);
}

int ScanService::get_hosts_found(int scan_id)
{
    return runtime_.get_hosts_found(scan_id);
}

void ScanService::on_nmap_eta(int scan_id, int seconds)
{
    runtime_.update_eta(scan_id, seconds);
}

void ScanService::on_nmap_hosts_found(int scan_id, int count)
{
    runtime_.update_hosts_found(scan_id, count);
}

ScanService::StartAsyncResult ScanService::start_async(const ScanRequest& request)
{
    const RequestValidationResult validation = validate_request(request);
    if (!validation.ok)
        return {StartAsyncStatus::ValidationError, -1, validation.error_message};

    ScanRequest normalized_request = request;
    normalized_request.target = canonicalize_target(request.target);
    if (!normalized_request.ports.empty())
        normalized_request.ports = validation.normalized_ports;
    normalized_request.nmap_executable = nmap_path_;

    {
        const StartAsyncResult cooldown_check = check_cooldown_policy(normalized_request);
        if (cooldown_check.status != StartAsyncStatus::Accepted)
            return cooldown_check;
    }

    {
        const StartAsyncResult dependency_check = check_dependencies();
        if (dependency_check.status != StartAsyncStatus::Accepted)
            return dependency_check;
    }

    return dispatch_scan_worker(normalized_request);
}

ScanService::StartAsyncResult ScanService::check_cooldown_policy(const ScanRequest& normalized_request)
{
    if (!settings_)
        return {StartAsyncStatus::Accepted, -1, ""};

    const int cooldown = settings_->load_settings().scan_cooldown_seconds;
    return check_cooldown(normalized_request.target, cooldown);
}

ScanService::StartAsyncResult ScanService::check_dependencies() const
{
    if (!nmap_path_.empty())
        return check_configured_nmap_path();

    NmapCheckResult nmap_check = check_nmap();
    if (nmap_check.status == HealthCheckStatus::Missing)
        return {StartAsyncStatus::DependencyMissing, -1, "nmap not found in PATH"};
    if (nmap_check.status != HealthCheckStatus::Ok)
    {
        const std::string detail = nmap_check.detail.empty()
            ? "nmap availability check failed"
            : nmap_check.detail;
        return {StartAsyncStatus::Error, -1, detail};
    }

    return {StartAsyncStatus::Accepted, -1, ""};
}

ScanService::StartAsyncResult ScanService::check_configured_nmap_path() const
{
    if (file_exists(nmap_path_))
        return {StartAsyncStatus::Accepted, -1, ""};

    return {StartAsyncStatus::DependencyMissing, -1,
            "configured nmap path not found: " + nmap_path_};
}

// Returns a rejected result if the chunk plan violates policy, or Accepted otherwise.
ScanService::StartAsyncResult ScanService::check_chunk_scan_policies(
    const ScanChunkPlan& chunk_plan) const
{
    if (!chunk_plan.chunked)
        return {StartAsyncStatus::Accepted, -1, ""};

    if (chunk_plan.requests.size() > max_scan_chunk_count())
        return {StartAsyncStatus::PolicyViolation, -1, "scan target too large for chunked port scan"};

    if (runtime_.active_chunked_scan_count() >= MAX_ACTIVE_CHUNKED_SCANS)
        return {StartAsyncStatus::PolicyViolation, -1, "too many chunked scans are already running"};

    return {StartAsyncStatus::Accepted, -1, ""};
}

// Starts the worker thread for scan_id. On failure, cleans up runtime state
// and persists a failure record. Returns Accepted on success, Error on failure.
ScanService::StartAsyncResult ScanService::launch_worker_thread(
    int scan_id, const ScanRequest& normalized_request)
{
    const ScanStorage storage{db_, repo_, logger_};
    auto finished_flag = std::make_shared<std::atomic<bool>>(false);
    try
    {
        std::thread t(&ScanService::worker_thread, this, scan_id,
                      normalized_request, finished_flag);
        std::lock_guard<std::mutex> lock(workers_mutex_);
        workers_.push_back({std::move(t), finished_flag});
    }
    catch (...)
    {
        logger_.error("failed to start scan worker thread");
        runtime_.clear_active(scan_id);
        persist_scan_failure(storage, scan_id,
                             lsm::service::scan_messages::FAILED_TO_START_WORKER_THREAD);
        return {StartAsyncStatus::Error, -1, ""};
    }
    return {StartAsyncStatus::Accepted, scan_id, ""};
}

ScanService::StartAsyncResult ScanService::dispatch_scan_worker(const ScanRequest& normalized_request)
{
    const ScanStorage storage{db_, repo_, logger_};

    std::lock_guard<std::mutex> start_lock(start_mutex_);
    if (runtime_.is_target_active(normalized_request.target))
        return {StartAsyncStatus::Conflict, -1, "a scan for this target is already running"};

    const ScanChunkPlan chunk_plan = plan_scan_chunks(normalized_request);
    {
        const StartAsyncResult policy_check = check_chunk_scan_policies(chunk_plan);
        if (policy_check.status != StartAsyncStatus::Accepted)
            return policy_check;
    }

    const int scan_id = insert_queued_scan(storage, normalized_request);
    if (scan_id < 0)
        return {StartAsyncStatus::Error, -1, ""};

    runtime_.clear_transient_terminal(scan_id);
    runtime_.register_active(scan_id, normalized_request.target, chunk_plan.chunked);

    reap_finished_workers();

    return launch_worker_thread(scan_id, normalized_request);
}

// Validates a scan for deletion. Returns NotFound, Conflict, or Deleted (meaning: ok to delete).
// REQUIRES: caller holds db write lock (called inside db_.write lambda).
ScanService::DeleteScanStatus ScanService::validate_scan_deletable(
    const std::unique_ptr<PersistedScanSummary>& scan, int scan_id)
{
    if (!scan)
    {
        logger_.error("delete_scan could not find scan id " + std::to_string(scan_id));
        return DeleteScanStatus::NotFound;
    }

    if (scan->deleted)
    {
        logger_.error("delete_scan rejected already deleted scan id " + std::to_string(scan_id));
        return DeleteScanStatus::NotFound;
    }

    if (scan->state == PersistedScanState::Queued ||
        scan->state == PersistedScanState::Running)
    {
        logger_.error("delete_scan rejected scan id " + std::to_string(scan_id) +
                      " because state is not finished");
        return DeleteScanStatus::Conflict;
    }

    return DeleteScanStatus::Deleted;
}

// Soft-deletes a scan and commits the transaction. Returns Deleted on success, Error on failure.
// REQUIRES: caller holds db write lock (called inside db_.write lambda).
ScanService::DeleteScanStatus ScanService::perform_soft_delete(
    sqlite3* h, WriteTransaction& tx, int scan_id, const std::string& target)
{
    if (!repo_.soft_delete_scan_by_id(h, scan_id, now_utc_timestamp()))
    {
        logger_.error("delete_scan failed for scan id " + std::to_string(scan_id) +
                      " target=" + target);
        return DeleteScanStatus::Error;
    }

    logger_.info("scan deleted via UI: id=" + std::to_string(scan_id) +
                 " target=" + target);
    tx.commit();
    return DeleteScanStatus::Deleted;
}

ScanService::DeleteScanStatus ScanService::delete_scan(int scan_id)
{
    if (scan_id <= 0)
    {
        logger_.error("delete_scan rejected invalid scan id " + std::to_string(scan_id));
        return DeleteScanStatus::NotFound;
    }

    return db_.write(
        [this, scan_id](sqlite3* h)
        {
            WriteTransaction tx(h, &logger_);
            std::unique_ptr<PersistedScanSummary> scan = repo_.get_scan_by_id(h, scan_id);
            const DeleteScanStatus validation = validate_scan_deletable(scan, scan_id);
            if (validation != DeleteScanStatus::Deleted)
                return validation;
            return perform_soft_delete(h, tx, scan_id, scan->target);
        });
}

// Returns the termination status for a scan that was not found in the active runtime.
// REQUIRES: no db lock held by caller.
ScanService::TerminateScanStatus ScanService::resolve_inactive_scan_status(int scan_id)
{
    return db_.read(
        [this, scan_id](sqlite3* h)
        {
            std::unique_ptr<PersistedScanSummary> scan = repo_.get_scan_by_id(h, scan_id);
            if (!scan || scan->deleted)
                return TerminateScanStatus::NotFound;
            return TerminateScanStatus::Conflict;
        });
}

// Sends terminate to all handles in ctx. Returns true if all succeeded.
bool ScanService::terminate_abort_handles(int scan_id, const ScanRuntime::AbortContext& ctx)
{
    bool all_ok = true;
    for (const NmapProcessHandle& handle : ctx.handles)
    {
        std::string error;
        if (!terminate_nmap_process(handle, &error))
        {
            logger_.error("failed to abort scan id " + std::to_string(scan_id) + ": " + error);
            all_ok = false;
        }
    }
    return all_ok;
}

ScanService::TerminateScanStatus ScanService::terminate_scan(int scan_id)
{
    if (scan_id <= 0)
        return TerminateScanStatus::NotFound;

    if (runtime_.has_transient_terminal(scan_id))
        return TerminateScanStatus::Conflict;

    ScanRuntime::AbortContext ctx;
    const bool was_active = runtime_.request_abort(
        scan_id, lsm::service::scan_messages::SCAN_ABORTED_BY_USER, ctx);

    if (!was_active)
        return resolve_inactive_scan_status(scan_id);

    if (!terminate_abort_handles(scan_id, ctx))
        return TerminateScanStatus::Error;

    const bool stopped = runtime_.wait_stopped(scan_id, abort_wait_timeout_);
    if (!stopped)
    {
        std::unique_ptr<PersistedScanSummary> scan = get_scan(scan_id);
        if (scan && scan->state == PersistedScanState::Aborted)
            return TerminateScanStatus::Aborted;

        logger_.error("terminate_scan timeout while waiting for worker shutdown: scan id " +
                      std::to_string(scan_id));
        return TerminateScanStatus::Error;
    }
    return TerminateScanStatus::Aborted;
}

void ScanService::terminate_all_running_scans(const std::string& reason)
{
    if (!runtime_.has_active_scans())
        return;

    const auto handles = runtime_.request_abort_all(reason);

    for (auto it = handles.begin(); it != handles.end(); ++it)
    {
        std::string error;
        if (!terminate_nmap_process(it->second, &error))
            logger_.error("failed to terminate running scan during shutdown: " + error);
    }

    const bool stopped = runtime_.wait_all_stopped(abort_wait_timeout_);
    if (stopped)
        return;

    logger_.warn("shutdown timeout while waiting for running scans to stop; force-terminating "
                 "remaining nmap processes");
    force_terminate_handles_for_shutdown(handles, logger_);

    if (!runtime_.wait_all_stopped(abort_wait_timeout_))
        logger_.error("shutdown timeout while waiting for running scans to stop after force-kill");
}

void ScanService::worker_thread_body(int scan_id, const ScanRequest& request)
{
    const ScanStorage storage{db_, repo_, logger_};
    if (!mark_scan_running(storage, scan_id))
    {
        logger_.error("scan worker aborted: could not mark running, id=" +
                      std::to_string(scan_id));
        runtime_.clear_active(scan_id);
        return;
    }

    ScanExecutionResult execution = execute_scan_with_abort_check(scan_id, request);

    logger_.info("scan worker finished: id=" + std::to_string(scan_id) +
                 " outcome=" + execution.input.result.message);

    finalize_scan_completion(execution);
}

// Returns an execution result representing a pre-run abort.
ScanService::ScanExecutionResult ScanService::make_pre_abort_result(
    int scan_id, const ScanRequest& request)
{
    ScanExecutionResult execution;
    execution.input.scan_id = scan_id;
    execution.input.request = request;
    execution.input.result = aborted_scan_result(runtime_.abort_reason(scan_id));
    execution.input.finished_at = now_utc_timestamp();
    return execution;
}

// Runs the chunked scan path and fills execution.chunk_result and execution.input.result.
void ScanService::execute_chunked_scan(int scan_id, const ScanChunkPlan& chunk_plan,
                                       ScanExecutionResult& execution)
{
    const std::size_t total_chunks = chunk_plan.requests.size();
    logger_.info("scan worker using chunked nmap execution: id=" +
                 std::to_string(scan_id) + " chunks=" + std::to_string(total_chunks));

    std::atomic<std::size_t> next_chunk_index(0);
    std::atomic<std::size_t> completed_chunks(0);
    std::mutex chunk_progress_mutex;
    std::vector<int> chunk_progress(total_chunks, 0);
    int chunk_progress_sum = 0;

    const auto update_chunk_progress =
        [this, scan_id, total_chunks, &chunk_progress_mutex, &chunk_progress,
         &chunk_progress_sum](std::size_t chunk_index, int percent)
        {
            int scan_progress = 0;
            {
                std::lock_guard<std::mutex> guard(chunk_progress_mutex);
                const int clamped_percent = clamp_progress_percent(percent);
                if (chunk_index < chunk_progress.size() &&
                    clamped_percent > chunk_progress[chunk_index])
                {
                    chunk_progress_sum += clamped_percent - chunk_progress[chunk_index];
                    chunk_progress[chunk_index] = clamped_percent;
                }

                scan_progress = static_cast<int>(
                    (static_cast<std::size_t>(chunk_progress_sum) * 95) /
                    (total_chunks * 100));
            }
            if (scan_progress > 0)
                runtime_.update_progress(scan_id, clamp_progress_percent(scan_progress));
        };

    runtime_.update_command(scan_id, chunked_command_progress_summary(0, total_chunks));

    const ScanChunkProcessRunner runner =
        [this, scan_id, total_chunks, &next_chunk_index, &completed_chunks,
         &update_chunk_progress](const ProcessSpec& process)
        {
            if (runtime_.is_abort_requested(scan_id))
                return NmapRunResult{false, -1, "", "", runtime_.abort_reason(scan_id)};

            const std::size_t chunk_index = next_chunk_index.fetch_add(1);
            ChunkWorkerListener listener(*this, scan_id,
                                         [chunk_index, &update_chunk_progress](int percent)
                                         {
                                             update_chunk_progress(chunk_index, percent);
                                         });
            NmapRunResult result = run_nmap_process(process, &logger_, &listener);
            const std::size_t completed = completed_chunks.fetch_add(1) + 1;
            update_chunk_progress(chunk_index, 100);
            runtime_.update_command(scan_id,
                                    chunked_command_progress_summary(completed, total_chunks));
            return result;
        };

    execution.chunked = true;
    execution.chunk_result = run_scan_chunks_with_runner(chunk_plan.requests, runner, &logger_);
    execution.input.result.outcome = execution.chunk_result.outcome;
    execution.input.result.message = execution.chunk_result.message;
    execution.input.result.command = chunked_command_summary(chunk_plan.requests.size());
    execution.input.result.exit_code = execution.chunk_result.ok ? 0 : -1;
    execution.input.result.stderr_text = execution.chunk_result.stderr_text;
}

// Overrides outcome to Aborted in execution when abort was requested after the scan ran.
void ScanService::apply_post_run_abort_override(int scan_id, ScanExecutionResult& execution)
{
    if (!runtime_.is_abort_requested(scan_id))
        return;

    const std::string reason = runtime_.abort_reason(scan_id);
    execution.input.result.outcome = ScanOutcome::Aborted;
    execution.input.result.message = reason;
    if (execution.chunked)
    {
        execution.chunk_result.outcome = ScanOutcome::Aborted;
        execution.chunk_result.message = reason;
        execution.chunk_result.ok = false;
    }
}

ScanService::ScanExecutionResult
ScanService::execute_scan_with_abort_check(int scan_id, const ScanRequest& request)
{
    if (runtime_.is_abort_requested(scan_id))
        return make_pre_abort_result(scan_id, request);

    ScanExecutionResult execution;
    execution.input.scan_id = scan_id;
    execution.input.request = request;

    const ScanChunkPlan chunk_plan = plan_scan_chunks(request);
    if (chunk_plan.chunked)
    {
        execute_chunked_scan(scan_id, chunk_plan, execution);
    }
    else
    {
        ScanWorkerListener listener(*this, scan_id);
        execution.input.result = run_scan_safely(request, logger_, &listener);
    }

    execution.input.finished_at = now_utc_timestamp();
    apply_post_run_abort_override(scan_id, execution);
    return execution;
}

void ScanService::finalize_scan_completion(const ScanExecutionResult& execution)
{
    ScanCompletionOrchestrator orchestrator(db_, repo_, logger_);
    ScanCompletionResult completion = execution.chunked
        ? orchestrator.complete_chunked(execution.input, execution.chunk_result)
        : orchestrator.complete(execution.input);
    if (!completion.ok)
    {
        runtime_.remember_transient_terminal_and_clear_active(
            make_transient_summary(execution.input, completion));
    }
    else
    {
        runtime_.clear_transient_terminal(execution.input.scan_id);
        runtime_.clear_active(execution.input.scan_id);
    }
}

void ScanService::reap_finished_workers()
{
    std::lock_guard<std::mutex> lock(workers_mutex_);
    std::size_t i = 0;
    while (i < workers_.size())
    {
        if (workers_[i].finished_flag->load())
        {
            workers_[i].thread.join();
            workers_.erase(workers_.begin() + static_cast<std::ptrdiff_t>(i));
        }
        else
        {
            ++i;
        }
    }
}

void ScanService::worker_thread(int scan_id, ScanRequest request,
                                std::shared_ptr<std::atomic<bool>> finished_flag)
{
    logger_.info("scan worker started: id=" + std::to_string(scan_id) +
                 " target=" + request.target);
    try
    {
        worker_thread_body(scan_id, request);
    }
    catch (...)
    {
        std::string detail = "unknown exception";
        try { throw; }
        catch (const std::exception& e) { detail = std::string("what=") + e.what(); }
        catch (...) {}
        logger_.error("scan worker unhandled exception: id=" + std::to_string(scan_id) +
                      " " + detail);
        const ScanStorage storage{db_, repo_, logger_};
        persist_scan_failure(storage, scan_id, lsm::service::scan_messages::WORKER_THREAD_EXCEPTION);
        runtime_.clear_active(scan_id);
    }
    finished_flag->store(true);
}
