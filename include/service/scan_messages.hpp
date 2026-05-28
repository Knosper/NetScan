#ifndef SERVICE_SCAN_MESSAGES_HPP
#define SERVICE_SCAN_MESSAGES_HPP

namespace lsm
{
namespace service
{
namespace scan_messages
{

extern const char SCAN_COMPLETED[];
extern const char SCAN_ABORTED_BY_USER[];
extern const char SCAN_ABORTED_STALE_STARTUP[];
extern const char FAILED_TO_PARSE_RESULTS[];
extern const char FAILED_TO_PERSIST_COMPLETED_RESULTS[];
extern const char FAILED_TO_PERSIST_TERMINAL_STATE[];
extern const char FAILED_TO_START_WORKER_THREAD[];
extern const char FAILED_TO_MARK_RUNNING[];
extern const char WORKER_THREAD_EXCEPTION[];

} // namespace scan_messages
} // namespace service
} // namespace lsm

#endif
