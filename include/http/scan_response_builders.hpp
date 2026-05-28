#ifndef HTTP_SCAN_RESPONSE_BUILDERS_HPP
#define HTTP_SCAN_RESPONSE_BUILDERS_HPP

#include "db/scan_read_types.hpp"
#include "scan/scan_types.hpp"
#include "scan/scan_diff_types.hpp"
#include "service/i_scan_starter.hpp"
#include "http/responses.hpp"
#include "platform.hpp"
#include "httplib/httplib.h"

#include <nlohmann/json.hpp>
#include <vector>

nlohmann::json scan_summary_to_json(const PersistedScanSummary& scan);
nlohmann::json scan_summary_with_hosts_to_json(const PersistedScanSummary& scan,
                                               const std::vector<ScanHostSummary>& hosts);
nlohmann::json scan_status_body(const PersistedScanSummary* scan, int progress,
                                int eta_seconds = -1, int hosts_found = -1);
nlohmann::json scan_list_to_json(const std::vector<PersistedScanSummary>& scans);
nlohmann::json scan_diff_to_json(const ScanDiffModel& diff);
nlohmann::json acknowledgement_to_json(const ScanDiffAcknowledgementResult& result);

bool apply_start_async_common_error(const IScanStarter::StartAsyncResult& result,
                                    httplib::Response& res);

#endif
