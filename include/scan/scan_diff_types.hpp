#ifndef SCAN_SCAN_DIFF_TYPES_HPP
#define SCAN_SCAN_DIFF_TYPES_HPP

#include "scan/scan_types.hpp"
#include <string>
#include <vector>
#include <memory>

struct HostDiffEntry
{
    std::string ip;
    std::string name;
    std::string acknowledgement_key;
    bool        acknowledged = false;
};

struct HostDiffResult
{
    std::vector<HostDiffEntry> new_hosts;
    std::vector<HostDiffEntry> disappeared_hosts;
};

struct PortDiffEntry
{
    std::string ip;
    int         port = 0;
    std::string name;
    std::string service;
    std::string acknowledgement_key;
    bool        acknowledged = false;
};

struct ScanDiffAcknowledgementKey
{
    std::string category;
    std::string ip;
    int         port = 0;
    bool        has_port = false;
};

inline std::string scan_diff_acknowledgement_key_id(
    const ScanDiffAcknowledgementKey& key)
{
    return key.category + "|" + key.ip + "|" + std::to_string(key.port);
}

enum class ScanDiffAcknowledgementError
{
    None = 0,
    BadRequest,
    DatabaseError
};

struct ScanDiffAcknowledgementResult
{
    ScanDiffAcknowledgementKey   key;
    bool                         acknowledged = false;
    ScanDiffAcknowledgementError error = ScanDiffAcknowledgementError::None;
    std::string                  error_message;

    bool ok() const { return error == ScanDiffAcknowledgementError::None; }
};

struct PortDiffResult
{
    std::vector<PortDiffEntry> new_open_ports;
    std::vector<PortDiffEntry> disappeared_ports;
};

struct ScanDiffModel
{
    PersistedScanSummary                  scan;
    std::unique_ptr<PersistedScanSummary> baseline;
    HostDiffResult                        host_diff;
    PortDiffResult                        port_diff;
};

enum class ScanDiffError
{
    None = 0,
    NotFound,
    NotDiffable,
    DatabaseError
};

struct ScanDiffResult
{
    ScanDiffResult() = default;
    ScanDiffResult(ScanDiffResult&&) = default;
    ScanDiffResult& operator=(ScanDiffResult&&) = default;

    std::unique_ptr<ScanDiffModel> diff;
    ScanDiffError                  error = ScanDiffError::None;

    bool has_diff() const { return diff != nullptr; }

    std::unique_ptr<ScanDiffModel> take_diff() { return std::move(diff); }
};

#endif
