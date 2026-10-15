#include "scan/scan_request_validator.hpp"
#include "scan/port_spec_validator.hpp"
#include "scan/target_validation.hpp"

ScanRequestValidation validate_scan_request(const ScanRequest& request)
{
    if (request.target.empty())
        return {false, "field 'target' must not be empty", {}};
    if (!is_valid_scan_target(request.target))
        return {false, "invalid target format", {}};
    if (request.host_discovery_only && !request.ports.empty())
        return {false, "port specification is not allowed when host discovery only is enabled", {}};

    const PortSpecValidation ports = validate_port_spec(request.ports);
    if (!ports.ok)
        return {false, ports.error_message, {}};

    ScanRequest normalized = request;
    normalized.target = canonicalize_target(request.target);
    normalized.ports = ports.validated_spec;
    return {true, "", normalized};
}
