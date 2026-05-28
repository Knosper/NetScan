#include "service/scan_messages.hpp"

namespace lsm
{
namespace service
{
namespace scan_messages
{

const char SCAN_COMPLETED[] = "scan completed";
const char SCAN_ABORTED_BY_USER[] = "terminated by user";
const char SCAN_ABORTED_STALE_STARTUP[] =
    "netscan terminated unexpectedly before scan completion";
const char FAILED_TO_PARSE_RESULTS[] = "failed to parse scan results";
const char FAILED_TO_PERSIST_COMPLETED_RESULTS[] =
    "failed to persist completed scan results";
const char FAILED_TO_PERSIST_TERMINAL_STATE[] = "failed to persist terminal scan state";
const char FAILED_TO_START_WORKER_THREAD[] = "failed to start worker thread";
const char FAILED_TO_MARK_RUNNING[] = "failed to mark scan running";
const char WORKER_THREAD_EXCEPTION[] = "scan worker terminated due to unexpected exception";

} // namespace scan_messages
} // namespace service
} // namespace lsm
