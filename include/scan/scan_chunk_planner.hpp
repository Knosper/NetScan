#ifndef SCAN_CHUNK_PLANNER_HPP
#define SCAN_CHUNK_PLANNER_HPP

#include "scan/scan_types.hpp"
#include <cstddef>
#include <vector>

struct ScanChunkPlan
{
    bool chunked = false;
    std::vector<ScanRequest> requests;
};

std::size_t max_scan_chunk_count();
std::size_t scan_chunk_count(const ScanRequest& request);
ScanChunkPlan plan_scan_chunks(const ScanRequest& request);

#endif
