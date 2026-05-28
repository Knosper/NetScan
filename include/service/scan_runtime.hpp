#ifndef SERVICE_SCAN_RUNTIME_HPP
#define SERVICE_SCAN_RUNTIME_HPP

#include "scan/nmap_runner.hpp"
#include "scan/scan_types.hpp"

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// Encapsulates the mutable runtime state of active scans.
// All locking for active scan state is managed here.
// ScanService must not access active_scans_ or transient_terminal_scans_ directly.
class ScanRuntime
{
public:
    struct AbortContext
    {
        std::vector<NmapProcessHandle> handles;
    };

    // Returns progress for scan_id, or -1 if not active.
    int get_progress(int scan_id) const;

    // Returns true if any scan is currently active.
    bool has_active_scans() const;

    // Returns the number of active scans using chunked execution.
    std::size_t active_chunked_scan_count() const;

    struct StatusSnapshot
    {
        bool                                   has_active;
        int                                    active_id;
        std::unique_ptr<PersistedScanSummary>  transient;
        std::string                            command;
        int                                    progress = -1;
        int                                    eta_seconds = -1;
        int                                    hosts_found = -1;
    };

    // Returns has_active, active_id and latest transient terminal atomically under one lock.
    StatusSnapshot get_status_snapshot() const;

    // Returns true if a scan with the given target is currently active.
    bool is_target_active(const std::string& target) const;

    // Registers a new active scan entry (initial state).
    void register_active(int scan_id, const std::string& target, bool chunked = false);

    // Removes the active scan entry and notifies waiters.
    void clear_active(int scan_id);

    // Clears the transient terminal scan for scan_id.
    void clear_transient_terminal(int scan_id);

    // Stores a transient terminal scan (used when DB persist fails).
    void remember_transient_terminal(const PersistedScanSummary& scan);

    // Atomically stores transient terminal scan and clears active entry.
    void remember_transient_terminal_and_clear_active(const PersistedScanSummary& scan);

    // Returns the transient terminal scan for scan_id, or nullptr.
    std::unique_ptr<PersistedScanSummary> get_transient_terminal(int scan_id) const;

    // Returns true if abort was requested for scan_id.
    bool is_abort_requested(int scan_id) const;

    // Returns the abort reason for scan_id, or empty string.
    std::string abort_reason(int scan_id) const;

    // Marks abort requested and returns AbortContext. Returns false if not active.
    bool request_abort(int scan_id, const std::string& reason, AbortContext& ctx);

    // Called when nmap process starts; stores handle and checks abort flag.
    // Returns true if abort was already requested (caller should terminate).
    bool register_process(int scan_id, const NmapProcessHandle& handle);

    // Called when nmap process finishes; clears handle.
    void unregister_process(int scan_id);
    void unregister_process(int scan_id, const NmapProcessHandle& handle);

    // Updates progress for an active scan.
    void update_progress(int scan_id, int percent);

    // Stores the nmap command string for an active scan.
    void update_command(int scan_id, const std::string& cmd);

    // Returns the stored command for an active scan, or empty string.
    std::string get_command(int scan_id) const;

    // Updates the ETA (seconds remaining) for an active scan.
    void update_eta(int scan_id, int seconds);

    // Returns the ETA for an active scan, or -1 if not set.
    int get_eta(int scan_id) const;

    // Updates the live hosts-found count for an active scan.
    void update_hosts_found(int scan_id, int count);

    // Returns the live hosts-found count, or -1 if not set.
    int get_hosts_found(int scan_id) const;

    // Marks all active scans for abort; returns handles to terminate.
    std::vector<std::pair<int, NmapProcessHandle>> request_abort_all(const std::string& reason);

    // Waits until all active scans are cleared or timeout.
    // Returns true if all scans stopped before timeout.
    bool wait_all_stopped(std::chrono::milliseconds timeout);

    // Waits until scan_id is no longer active or timeout.
    // Returns true if stopped before timeout.
    bool wait_stopped(int scan_id, std::chrono::milliseconds timeout);

    // Returns true if scan_id has a transient terminal record.
    bool has_transient_terminal(int scan_id) const;

private:
    struct RuntimeState
    {
        bool              active           = false;
        std::vector<NmapProcessHandle> process_handles;
        bool              abort_requested  = false;
        std::string       abort_reason;
        int               progress         = -1;
        int               eta_seconds      = -1;
        int               hosts_found      = -1;
        std::string       target;
        std::string       command;
        bool              chunked          = false;
    };

    mutable std::mutex              mutex_;
    std::condition_variable         state_changed_;
    std::unordered_map<int, RuntimeState>          active_scans_;
    std::unordered_map<int, PersistedScanSummary>  transient_terminal_scans_;
};

#endif
