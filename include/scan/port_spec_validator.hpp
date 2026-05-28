#pragma once
#include "scan/scan_types.hpp"

#include <string>

struct PortSpecValidation
{
    bool        ok;
    std::string error_message;
    std::string validated_spec;
};

PortSpecValidation validate_port_spec(const std::string& spec);
ScanPortCoverage derive_scan_port_coverage(const ScanRequest& request);
