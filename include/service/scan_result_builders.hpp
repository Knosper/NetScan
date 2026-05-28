#ifndef SERVICE_SCAN_RESULT_BUILDERS_HPP
#define SERVICE_SCAN_RESULT_BUILDERS_HPP

#include "scan/scan_types.hpp"

#include <string>

ScanResult failed_scan_result(const std::string& message);
ScanResult aborted_scan_result(const std::string& message);

#endif
