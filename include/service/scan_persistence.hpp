#ifndef SERVICE_SCAN_PERSISTENCE_HPP
#define SERVICE_SCAN_PERSISTENCE_HPP

#include "db/scan_repository.hpp"
#include "scan/scan_snapshot.hpp"
#include "util/logger.hpp"

#include <sqlite3.h>
#include <vector>

struct CompletedScanSnapshot
{
    int                         scan_id = -1;
    bool                        host_discovery_only = true;
    ScanPortCoverage            port_coverage;
    lsm::scan::ScanSnapshot     snapshot;
};

// Precondition: caller must hold the DB write-scope lock and have an active
// transaction (BEGIN IMMEDIATE) before calling this function. Scan completion
// starts that transaction before snapshot persistence and marks the scan
// completed only after this function succeeds.
// All persistence operations are part of that transaction — calling this
// outside a write-scope or without an active transaction is undefined behaviour.
bool persist_completed_scan_snapshot(sqlite3* h, ScanRepository& repo, Logger& logger,
                                     const CompletedScanSnapshot& snapshot);

#endif
