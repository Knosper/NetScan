#ifndef SERVER_HPP
#define SERVER_HPP

#include "app/auth_config.hpp"
#include "platform.hpp"
#include "app/app_context.hpp"
#include "http/routes.hpp"
#include "httplib/httplib.h"

#include <memory>
#include <string>

struct ServerImpl
{
    virtual bool bind_to_port(const std::string& host, int port) = 0;
    virtual bool listen_after_bind() = 0;
    virtual void stop() = 0;
    virtual httplib::Server& raw() = 0;
    virtual ~ServerImpl() = default;
};

class Server
{
private:
    std::unique_ptr<ServerImpl> impl_;

public:
    Server(const AppContext& ctx, const AuthConfig& auth_config, ApiServices& services);
    bool bind(const std::string& host, int port);
    bool listen();
    void stop();
};

#endif
