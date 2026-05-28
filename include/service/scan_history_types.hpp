#ifndef SERVICE_SCAN_HISTORY_TYPES_HPP
#define SERVICE_SCAN_HISTORY_TYPES_HPP

#include "scan/scan_types.hpp"

#include <string>
#include <vector>

struct ScanFilter
{
    std::string target;
    std::string state;
    std::string from;
    std::string to;
    int         limit  = 0;
    int         offset = 0;
};

enum class ListFilteredScansStatus { Ok, ValidationError };

struct FilteredScansResult
{
    ListFilteredScansStatus          status = ListFilteredScansStatus::Ok;
    std::vector<PersistedScanSummary> scans;
    std::string                      error_message;
};

#endif
