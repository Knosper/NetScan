#ifndef SCAN_REQUEST_VALIDATOR_HPP
#define SCAN_REQUEST_VALIDATOR_HPP

#include "scan/scan_types.hpp"

struct ScanRequestValidation
{
    bool ok = false;
    std::string error_message;
    ScanRequest request;
};

ScanRequestValidation validate_scan_request(const ScanRequest& request);

#endif
