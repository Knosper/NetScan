#pragma once

#include <string>
#include "scan/scan_snapshot.hpp"

namespace lsm {
namespace scan {

struct NmapXmlParseResult
{
    bool ok = false;
    ScanSnapshot snapshot;
    std::string error;
};

NmapXmlParseResult parse_nmap_xml(const std::string& xml);

} // namespace scan
} // namespace lsm