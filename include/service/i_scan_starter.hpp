#ifndef SERVICE_I_SCAN_STARTER_HPP
#define SERVICE_I_SCAN_STARTER_HPP

#include "scan/scan_types.hpp"
#include <string>

class IScanStarter
{
public:
    virtual ~IScanStarter() {}

    enum class StartAsyncStatus
    {
        Accepted,
        ValidationError,
        PolicyViolation,
        Conflict,
        DependencyMissing,
        Error
    };

    struct StartAsyncResult
    {
        StartAsyncStatus status = StartAsyncStatus::Error;
        int              scan_id = -1;
        std::string      error_message;
    };

    virtual StartAsyncResult start_async(const ScanRequest& request) = 0;
};

#endif
