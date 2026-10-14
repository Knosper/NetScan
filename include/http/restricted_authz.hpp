#ifndef HTTP_RESTRICTED_AUTHZ_HPP
#define HTTP_RESTRICTED_AUTHZ_HPP

#include "http/http_status.hpp"
#include "http/responses.hpp"
#include "scan/target_validation.hpp"
#include "service/scan_service.hpp"
#include "service/settings_service.hpp"

#include <memory>
#include <string>

inline bool allow_restricted_target(const httplib::Request& req, httplib::Response& res,
                                    const std::string& target,
                                    const SettingsService& settings_service)
{
    if (!req.has_header("X-NetScan-Restricted-Key") ||
        is_user_target_allowed(target, settings_service.list_allowed_targets()))
        return true;

    set_json_error(res, JsonError{http_status::FORBIDDEN, "forbidden",
                                  "target not allowed for restricted key"});
    return false;
}

inline bool allow_restricted_scan_id(const httplib::Request& req, httplib::Response& res,
                                     int scan_id, ScanService& scan_service,
                                     const SettingsService& settings_service)
{
    if (!req.has_header("X-NetScan-Restricted-Key"))
        return true;

    const std::unique_ptr<PersistedScanSummary> scan = scan_service.get_scan(scan_id);
    // The route keeps its existing missing/deleted-resource response.
    return !scan || allow_restricted_target(req, res, scan->target, settings_service);
}

#endif
