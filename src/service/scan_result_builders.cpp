#include "service/scan_result_builders.hpp"

ScanResult failed_scan_result(const std::string& message)
{
    ScanResult result;
    result.outcome = ScanOutcome::Failed;
    result.message = message;
    result.exit_code = -1;
    return result;
}

ScanResult aborted_scan_result(const std::string& message)
{
    ScanResult result;
    result.outcome = ScanOutcome::Aborted;
    result.message = message;
    result.exit_code = -1;
    return result;
}
