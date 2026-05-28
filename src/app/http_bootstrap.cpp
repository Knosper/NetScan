#include "app/http_bootstrap.hpp"

#include "app/application_bootstrap.hpp"
#include "app/paths.hpp"
#include "util/file_permissions.hpp"
#include "util/path_utils.hpp"
#include "httplib/httplib.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#else
#include <signal.h>
#endif
#ifndef _WIN32
#include <unistd.h>
#endif

std::string bind_error_message(const std::string& host, int port)
{
    const int err = errno;
    const std::string addr = host + ":" + std::to_string(port);
    const std::string reason = err != 0 ? std::strerror(err) : "unknown error";
    return "Failed to bind to " + addr + ": " + reason;
}

namespace
{
bool probe_lsm_instance(const std::string& host, int port)
{
    httplib::Client client(host, port);
    client.set_connection_timeout(2, 0);
    client.set_read_timeout(2, 0);
    const auto result = client.Get("/api/health");
    return result && result->status == 200;
}

constexpr int PORT_RETRY_RANGE = 100;

static void shutdown_services(ServerRunContext& ctx)
{
    ctx.presence_scheduler.stop();
    ctx.scheduler.stop();
    ctx.scan_service.terminate_all_running_scans("terminated during shutdown");
    ctx.server.stop();
}

// Tries to bind server on configured_port, then configured_port+1 … +PORT_RETRY_RANGE.
// Only retries on 127.0.0.1: non-loopback deployments expect a fixed port (firewall,
// reverse-proxy) so a silent port change there would break the deployment silently.
// Returns the bound port on success, -1 on failure.
static int bind_with_retry(Server& server, const std::string& host, int configured_port,
                           Logger& logger)
{
    if (server.bind(host, configured_port))
        return configured_port;

    if (host != "127.0.0.1")
    {
        logger.error(bind_error_message(host, configured_port));
        return -1;
    }

    logger.warn("Port " + std::to_string(configured_port) +
                " is in use, searching for a free port in range " +
                std::to_string(configured_port) + "-" +
                std::to_string(configured_port + PORT_RETRY_RANGE));

    for (int p = configured_port + 1; p <= configured_port + PORT_RETRY_RANGE; ++p)
    {
        if (server.bind(host, p))
        {
            logger.warn("configured port " + std::to_string(configured_port) +
                        " was in use, fell back to " + std::to_string(p));
            return p;
        }
    }

    logger.error("All ports in range " + std::to_string(configured_port) + "-" +
                 std::to_string(configured_port + PORT_RETRY_RANGE) +
                 " are in use on " + host);
    return -1;
}

static void persist_bound_port(const std::string& config_path, int port, Logger& logger)
{
    PersistedConfig persisted = load_persisted_config(config_path);
    persisted.port = port;
    std::string error;
    if (!write_persisted_config_atomically(config_path, persisted, error))
        logger.error("Failed to persist bound port to conf.ini: " + error);
}

static unsigned long current_process_id()
{
#ifdef _WIN32
    return static_cast<unsigned long>(GetCurrentProcessId());
#else
    return static_cast<unsigned long>(getpid());
#endif
}

static std::string pid_file_path_for_config(const std::string& config_path)
{
    return join_path(dir_of(config_path), "netscan.pid");
}

static bool process_exists(unsigned long pid)
{
    if (pid == 0)
        return false;
#ifdef _WIN32
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (process == nullptr)
        return false;
    DWORD wait_result = WaitForSingleObject(process, 0);
    CloseHandle(process);
    return wait_result == WAIT_TIMEOUT;
#else
    return kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
#endif
}

static void remove_stale_pid_file(const std::string& pid_file_path)
{
    std::ifstream in(pid_file_path.c_str());
    if (!in)
        return;

    unsigned long stored_pid = 0;
    in >> stored_pid;
    if (stored_pid == 0 || !process_exists(stored_pid))
        std::remove(pid_file_path.c_str());
}

static bool write_pid_file(const std::string& pid_file_path, Logger& logger)
{
    std::ofstream out(pid_file_path.c_str(), std::ios::out | std::ios::trunc);
    if (!out)
    {
        logger.warn("Failed to write PID file: " + pid_file_path);
        return false;
    }

    out << current_process_id() << "\n";
    out.close();
    if (!out)
    {
        logger.warn("Failed to flush PID file: " + pid_file_path);
        return false;
    }

    std::string permission_error;
    if (!util::restrict_file_to_owner(pid_file_path, &permission_error))
        logger.warn("Failed to restrict PID file permissions: " + permission_error);
    return true;
}

static void remove_pid_file_if_owned(const std::string& pid_file_path)
{
    std::ifstream in(pid_file_path.c_str());
    if (!in)
        return;

    unsigned long stored_pid = 0;
    in >> stored_pid;
    if (stored_pid == current_process_id())
        std::remove(pid_file_path.c_str());
}

static int run_and_handle_result(ServerRunContext& ctx, const std::string& pid_file_path,
                                 int bound_port)
{
    std::thread watcher = ctx.shutdown.start_watcher(
        [&ctx]() { shutdown_services(ctx); },
        ctx.logger);

    const bool ok = ctx.server.listen();
    ctx.shutdown.request();
    watcher.join();
    remove_pid_file_if_owned(pid_file_path);

    if (!ok)
    {
        ctx.logger.error("Server stopped unexpectedly on " + ctx.config.host + ":" +
                         std::to_string(bound_port));
        return 1;
    }

    if (ShutdownController::restart_requested())
    {
        ctx.logger.info("Server stopped. Restarting.");
        return EXIT_RESTART;
    }

    ctx.logger.info("Server stopped. Shutting down.");
    return 0;
}

} // namespace

int run_server(ServerRunContext ctx)
{
    const std::string scheme = ctx.config.tls_enabled ? "https" : "http";
    const std::string base_url =
        scheme + "://" + ctx.config.host + ":" + std::to_string(ctx.config.port);

    if (!ctx.config.tls_enabled && probe_lsm_instance(ctx.config.host, ctx.config.port))
    {
        ctx.logger.info("NetScan is already running at " + base_url);
        shutdown_services(ctx);
        return 1;
    }

    const int bound_port =
        bind_with_retry(ctx.server, ctx.config.host, ctx.config.port, ctx.logger);
    if (bound_port < 0)
    {
        shutdown_services(ctx);
        return 1;
    }

    if (bound_port != ctx.config.port)
        persist_bound_port(ctx.config_path, bound_port, ctx.logger);

    const std::string pid_file_path = pid_file_path_for_config(ctx.config_path);
    remove_stale_pid_file(pid_file_path);
    write_pid_file(pid_file_path, ctx.logger);

    const std::string actual_url =
        scheme + "://" + ctx.config.host + ":" + std::to_string(bound_port);
    ctx.logger.info("Server running on " + actual_url);

    return run_and_handle_result(ctx, pid_file_path, bound_port);
}
