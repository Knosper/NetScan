#ifndef HOST_HOST_TYPES_HPP
#define HOST_HOST_TYPES_HPP

#include "scan/scan_types.hpp"
#include <memory>
#include <string>
#include <vector>

struct HostMeta
{
    std::string display_name;
    std::string role;
    std::string tags;
};

struct HostOverview
{
    int         id = 0;
    std::string ip;
    std::string name;
    int         scan_count = 0;
    std::string last_seen_at;
    int         last_scan_id = 0;
    int         open_port_count = 0;
    HostMeta    meta;
};

struct HostSummary
{
    int         id = 0;
    std::string ip;
    std::string name;
};

struct HostHistoryScan
{
    int                id = 0;
    PersistedScanState state = PersistedScanState::Completed;
    std::string        created_at;
    std::string        started_at;
    std::string        finished_at;
    std::string        deleted_at;
    bool               deleted = false;
};

struct HostHistoryPort
{
    int         port = 0;
    std::string service;
    std::string state;
};

struct HostHistoryEntry
{
    HostHistoryScan              scan;
    std::vector<HostHistoryPort> ports;
};

struct HostDetail
{
    HostSummary                   host;
    HostMeta                      meta;
    std::vector<HostHistoryEntry> history;
};

#endif
