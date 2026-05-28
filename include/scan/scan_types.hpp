#ifndef SCAN_TYPES_HPP
#define SCAN_TYPES_HPP

#include <string>
#include <vector>

inline int clamp_progress_percent(int percent)
{
    if (percent < 0)
        return 0;
    if (percent > 100)
        return 100;
    return percent;
}

enum class PersistedScanState
{
    Queued,
    Running,
    Aborted,
    Completed,
    Failed,
    DependencyMissing
};

// Returns the canonical status value stored in the database.
inline const char* to_db_value(PersistedScanState state)
{
    switch (state)
    {
    case PersistedScanState::Queued:
        return "queued";
    case PersistedScanState::Running:
        return "running";
    case PersistedScanState::Aborted:
        return "aborted";
    case PersistedScanState::Completed:
        return "completed";
    case PersistedScanState::Failed:
        return "failed";
    case PersistedScanState::DependencyMissing:
        return "dependency_missing";
    }

    return "failed";
}

// Returns the status label exposed via JSON/API payloads.
inline const char* to_api_status(PersistedScanState state)
{
    return to_db_value(state);
}

inline PersistedScanState persisted_state_from_db(const std::string& value)
{
    if (value == "queued")
        return PersistedScanState::Queued;
    if (value == "running")
        return PersistedScanState::Running;
    if (value == "aborted")
        return PersistedScanState::Aborted;
    if (value == "completed")
        return PersistedScanState::Completed;
    if (value == "dependency_missing")
        return PersistedScanState::DependencyMissing;
    return PersistedScanState::Failed;
}

struct PersistedScanSummary
{
    int                  id = 0;
    std::string          target;
    bool                 host_discovery_only = true;
    std::string          requested_ports;
    bool                 port_coverage_known = false;
    PersistedScanState   state = PersistedScanState::Queued;
    std::string          message;
    std::string          command;
    int                  exit_code = -1;
    bool                 has_exit_code = false;
    std::string          stderr_text;
    std::string          created_at;
    std::string          started_at;
    std::string          finished_at;
    std::string          deleted_at;
    bool                 deleted = false;
};

struct ScanRequest
{
    std::string target;
    std::string ports;
    bool        host_discovery_only = false;
    // When non-empty, used as the nmap executable path instead of PATH lookup.
    std::string nmap_executable;
};

struct PortCoverageRange
{
    int start_port = 0;
    int end_port = 0;
};

struct ScanPortCoverage
{
    bool                           known = false;
    std::vector<PortCoverageRange> tcp_ranges;
};

enum class ScanOutcome
{
    Completed,
    Rejected,
    Aborted,
    DependencyMissing,
    Failed
};

struct ScanResult
{
    ScanOutcome outcome = ScanOutcome::Failed;
    std::string message;
    std::string command;
    int exit_code = -1;
    std::string raw_output;
    std::string stderr_text;
};

inline const char* to_string(ScanOutcome outcome)
{
    switch (outcome)
    {
    case ScanOutcome::Completed:
        return "completed";
    case ScanOutcome::Rejected:
        return "rejected";
    case ScanOutcome::Aborted:
        return "aborted";
    case ScanOutcome::DependencyMissing:
        return "dependency_missing";
    case ScanOutcome::Failed:
        return "failed";
    }

    return "failed";
}

#endif
