#include "http/server.hpp"

namespace
{
struct HttpServerImpl : ServerImpl
{
    httplib::Server svr;

    bool bind_to_port(const std::string& host, int port) override
    {
        return svr.bind_to_port(host, port);
    }
    bool listen_after_bind() override { return svr.listen_after_bind(); }
    void stop() override { svr.stop(); }
    httplib::Server& raw() override { return svr; }
};

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
struct HttpsServerImpl : ServerImpl
{
    httplib::SSLServer svr;

    HttpsServerImpl(const std::string& cert_path, const std::string& key_path)
        : svr(cert_path.c_str(), key_path.c_str())
    {}

    bool bind_to_port(const std::string& host, int port) override
    {
        return svr.bind_to_port(host, port);
    }
    bool listen_after_bind() override { return svr.listen_after_bind(); }
    void stop() override { svr.stop(); }
    httplib::Server& raw() override { return svr; }
};
#endif
} // namespace

Server::Server(const AppContext& ctx, const AuthConfig& auth_config, ApiServices& services)
{
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    if (ctx.config.tls_enabled)
        impl_ = std::unique_ptr<ServerImpl>(
            new HttpsServerImpl(ctx.config.tls_cert_path, ctx.config.tls_key_path));
    else
        impl_ = std::unique_ptr<ServerImpl>(new HttpServerImpl());
#else
    impl_ = std::unique_ptr<ServerImpl>(new HttpServerImpl());
#endif
    register_routes(impl_->raw(), ctx, auth_config, services);
}

bool Server::bind(const std::string& host, int port)
{
    return impl_->bind_to_port(host, port);
}

bool Server::listen()
{
    return impl_->listen_after_bind();
}

void Server::stop()
{
    impl_->stop();
}
