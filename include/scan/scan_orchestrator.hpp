#ifndef SCAN_ORCHESTRATOR_HPP
#define SCAN_ORCHESTRATOR_HPP

#include "scan/nmap_runner.hpp"
#include "scan/scan_types.hpp"
#include "util/logger.hpp"

class ScanOrchestrator
{
public:
    explicit ScanOrchestrator(Logger& logger, NmapProcessListener* listener = nullptr);
    ScanResult run_sync(const ScanRequest& request) const;

private:
    Logger& logger_;
    NmapProcessListener* listener_;
};

#endif
