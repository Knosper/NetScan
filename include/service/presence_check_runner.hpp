#ifndef SERVICE_PRESENCE_CHECK_RUNNER_HPP
#define SERVICE_PRESENCE_CHECK_RUNNER_HPP

#include "service/presence_service.hpp"

#include <string>

#include "util/logger.hpp"

struct HttpTarget
{
    std::string host;
    int port = 80;
    std::string path = "/";
};

bool parse_http_url(const PresenceTracker& tracker, HttpTarget& target, std::string& error);

class DefaultPresenceCheckRunner : public IPresenceCheckRunner
{
public:
    explicit DefaultPresenceCheckRunner(Logger* logger = nullptr);
    PresenceCheckResult run(const PresenceTracker& tracker) override;

private:
    Logger* logger_;
};

#endif
