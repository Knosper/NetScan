#ifndef SCAN_SNAPSHOT_MERGER_HPP
#define SCAN_SNAPSHOT_MERGER_HPP

#include "scan/scan_snapshot.hpp"
#include <string>
#include <vector>

namespace lsm {
namespace scan {

struct ScanSnapshotMergeResult
{
    bool ok = false;
    ScanSnapshot snapshot;
    std::string error;
};

ScanSnapshot merge_scan_snapshots(const std::vector<ScanSnapshot>& snapshots);
ScanSnapshotMergeResult merge_nmap_xml_snapshots(const std::vector<std::string>& xml_outputs);

} // namespace scan
} // namespace lsm

#endif
