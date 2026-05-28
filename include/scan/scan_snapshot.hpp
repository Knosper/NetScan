#pragma once

#include <string>
#include <vector>

namespace lsm {
namespace scan {

struct PortEntry {
    std::string protocol;
    int port = 0;
    std::string state;
    std::string service;
};

struct HostSnapshot {
    std::string ip;
    std::string hostname;
    std::vector<PortEntry> ports;
};

struct ScanSnapshot {
    std::vector<HostSnapshot> hosts;
};

} // namespace scan
} // namespace lsm
