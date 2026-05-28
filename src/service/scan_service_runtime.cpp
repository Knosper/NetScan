#include "service/scan_runtime.hpp"

#include <algorithm>

namespace
{
bool same_process_handle(const NmapProcessHandle& left, const NmapProcessHandle& right)
{
#ifdef _WIN32
    return left.process == right.process && left.job == right.job;
#else
    return left.pid == right.pid && left.process_group_id == right.process_group_id;
#endif
}
} // namespace

int ScanRuntime::get_progress(int scan_id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = active_scans_.find(scan_id);
    if (it == active_scans_.end())
        return -1;
    return it->second.progress;
}

bool ScanRuntime::has_active_scans() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return !active_scans_.empty();
}

std::size_t ScanRuntime::active_chunked_scan_count() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t count = 0;
    for (auto it = active_scans_.begin(); it != active_scans_.end(); ++it)
    {
        if (it->second.chunked)
            ++count;
    }
    return count;
}

ScanRuntime::StatusSnapshot ScanRuntime::get_status_snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    StatusSnapshot snap;
    snap.has_active = !active_scans_.empty();
    snap.active_id  = -1;
    for (auto it = active_scans_.begin(); it != active_scans_.end(); ++it)
    {
        if (it->first > snap.active_id)
            snap.active_id = it->first;
    }

    for (auto it = transient_terminal_scans_.begin(); it != transient_terminal_scans_.end(); ++it)
    {
        if (!snap.transient || it->first > snap.transient->id)
            snap.transient = std::make_unique<PersistedScanSummary>(it->second);
    }

    if (snap.has_active && snap.active_id > 0)
    {
        std::unordered_map<int, RuntimeState>::const_iterator active =
            active_scans_.find(snap.active_id);
        if (active != active_scans_.end())
        {
            snap.command = active->second.command;
            snap.progress = active->second.progress;
            snap.eta_seconds = active->second.eta_seconds;
            snap.hosts_found = active->second.hosts_found;
        }
    }

    if (snap.transient && snap.transient->id == snap.active_id)
    {
        snap.has_active = false;
        snap.active_id = -1;
        snap.command.clear();
        snap.progress = -1;
        snap.eta_seconds = -1;
        snap.hosts_found = -1;
    }

    return snap;
}

bool ScanRuntime::is_target_active(const std::string& target) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = active_scans_.begin(); it != active_scans_.end(); ++it)
    {
        if (it->second.target == target)
            return true;
    }
    return false;
}

void ScanRuntime::register_active(int scan_id, const std::string& target, bool chunked)
{
    std::lock_guard<std::mutex> lock(mutex_);
    RuntimeState state;
    state.target = target;
    state.chunked = chunked;
    active_scans_[scan_id] = state;
}

void ScanRuntime::clear_active(int scan_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    active_scans_.erase(scan_id);
    state_changed_.notify_all();
}

void ScanRuntime::clear_transient_terminal(int scan_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    transient_terminal_scans_.erase(scan_id);
}

void ScanRuntime::remember_transient_terminal(const PersistedScanSummary& scan)
{
    std::lock_guard<std::mutex> lock(mutex_);
    transient_terminal_scans_[scan.id] = scan;
}

void ScanRuntime::remember_transient_terminal_and_clear_active(const PersistedScanSummary& scan)
{
    std::lock_guard<std::mutex> lock(mutex_);
    transient_terminal_scans_[scan.id] = scan;
    active_scans_.erase(scan.id);
    state_changed_.notify_all();
}

std::unique_ptr<PersistedScanSummary> ScanRuntime::get_transient_terminal(int scan_id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = transient_terminal_scans_.find(scan_id);
    if (it == transient_terminal_scans_.end())
        return nullptr;
    return std::make_unique<PersistedScanSummary>(it->second);
}

bool ScanRuntime::is_abort_requested(int scan_id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = active_scans_.find(scan_id);
    return it != active_scans_.end() && it->second.abort_requested;
}

std::string ScanRuntime::abort_reason(int scan_id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = active_scans_.find(scan_id);
    if (it == active_scans_.end())
        return std::string();
    return it->second.abort_reason;
}

bool ScanRuntime::request_abort(int scan_id, const std::string& reason, AbortContext& ctx)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = active_scans_.find(scan_id);
    if (it == active_scans_.end())
        return false;

    it->second.abort_requested = true;
    it->second.abort_reason    = reason;
    ctx.handles = it->second.process_handles;

    // Atomically take ownership of the handle so unregister_process finds it gone.
    // This prevents terminate_nmap_process from using a PID that has been recycled.
    it->second.process_handles.clear();
    return true;
}

bool ScanRuntime::register_process(int scan_id, const NmapProcessHandle& handle)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = active_scans_.find(scan_id);
    // Scan was already cleared (abort swept before fork landed).
    // Signal the caller to terminate the untracked process immediately.
    if (it == active_scans_.end())
        return true;

    it->second.process_handles.push_back(handle);
    return it->second.abort_requested;
}

void ScanRuntime::unregister_process(int scan_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = active_scans_.find(scan_id);
    if (it != active_scans_.end())
        it->second.process_handles.clear();
    state_changed_.notify_all();
}

void ScanRuntime::unregister_process(int scan_id, const NmapProcessHandle& handle)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = active_scans_.find(scan_id);
    if (it != active_scans_.end())
    {
        auto& handles = it->second.process_handles;
        handles.erase(std::remove_if(handles.begin(), handles.end(),
                                     [&handle](const NmapProcessHandle& current)
                                     { return same_process_handle(current, handle); }),
                      handles.end());
    }
    state_changed_.notify_all();
}

void ScanRuntime::update_progress(int scan_id, int percent)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = active_scans_.find(scan_id);
    if (it != active_scans_.end())
        it->second.progress = percent;
}

void ScanRuntime::update_command(int scan_id, const std::string& cmd)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = active_scans_.find(scan_id);
    if (it != active_scans_.end())
        it->second.command = cmd;
}

std::string ScanRuntime::get_command(int scan_id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = active_scans_.find(scan_id);
    if (it == active_scans_.end())
        return std::string();
    return it->second.command;
}

void ScanRuntime::update_eta(int scan_id, int seconds)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = active_scans_.find(scan_id);
    if (it != active_scans_.end())
        it->second.eta_seconds = seconds;
}

int ScanRuntime::get_eta(int scan_id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = active_scans_.find(scan_id);
    if (it == active_scans_.end())
        return -1;
    return it->second.eta_seconds;
}

void ScanRuntime::update_hosts_found(int scan_id, int count)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = active_scans_.find(scan_id);
    if (it != active_scans_.end())
        it->second.hosts_found = count;
}

int ScanRuntime::get_hosts_found(int scan_id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = active_scans_.find(scan_id);
    if (it == active_scans_.end())
        return -1;
    return it->second.hosts_found;
}

std::vector<std::pair<int, NmapProcessHandle>>
ScanRuntime::request_abort_all(const std::string& reason)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<int, NmapProcessHandle>> handles;

    for (auto it = active_scans_.begin(); it != active_scans_.end(); ++it)
    {
        it->second.abort_requested = true;
        it->second.abort_reason    = reason;
        for (const NmapProcessHandle& handle : it->second.process_handles)
        {
            handles.push_back(std::make_pair(it->first, handle));
        }
        // Atomically take ownership so unregister_process finds the handles gone.
        it->second.process_handles.clear();
    }
    return handles;
}

bool ScanRuntime::wait_all_stopped(std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(mutex_);
    return state_changed_.wait_for(lock, timeout,
                                   [this]() { return active_scans_.empty(); });
}

bool ScanRuntime::wait_stopped(int scan_id, std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(mutex_);
    return state_changed_.wait_for(lock, timeout, [this, scan_id]()
                                   { return active_scans_.find(scan_id) == active_scans_.end(); });
}

bool ScanRuntime::has_transient_terminal(int scan_id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return transient_terminal_scans_.find(scan_id) != transient_terminal_scans_.end();
}
