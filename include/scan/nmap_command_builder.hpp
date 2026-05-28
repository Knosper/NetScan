#ifndef NMAP_COMMAND_BUILDER_HPP
#define NMAP_COMMAND_BUILDER_HPP

#include "scan/process_spec.hpp"
#include "scan/scan_types.hpp"

struct NmapCommandBuildResult
{
    bool ok = false;
    ProcessSpec process;
    std::string error;
};

NmapCommandBuildResult build_nmap_command(const ScanRequest& request);

#endif
