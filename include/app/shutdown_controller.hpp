#ifndef SHUTDOWN_CONTROLLER_HPP
#define SHUTDOWN_CONTROLLER_HPP

#include "platform.hpp"
#include "util/logger.hpp"
#include <atomic>
#include <csignal>
#include <functional>
#include <thread>
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#endif

// Encapsulates shutdown state and OS signal handling.
//
// The internal flag is a static class member (signal-safe volatile sig_atomic_t)
// because POSIX signal handlers must only touch async-signal-safe storage, and
// OS signal handlers are process-wide singletons.  Consequently only one
// ShutdownController instance should be created per process.
class ShutdownController
{
public:
    // Register OS signal handlers (SIGINT/SIGTERM on POSIX, SetConsoleCtrlHandler on Windows).
    // Returns false if handler registration fails.
    bool setup(Logger& logger);

    // Start a background thread that waits for the shutdown flag and then runs the shutdown callback.
    // The caller MUST join the returned thread before objects captured by `on_shutdown` are destroyed.
    std::thread start_watcher(std::function<void()> on_shutdown, Logger& logger);

    // Programmatically request shutdown (e.g. after server.listen() returns on its own).
    static void request();

    // Programmatically request shutdown AND signal that the caller wants the
    // process to restart afterwards. main()'s restart loop returns EXIT_RESTART
    // instead of the normal exit code.
    static void request_restart();

    // True if request_restart() was called since the last setup().
    static bool restart_requested();

private:
    static volatile sig_atomic_t s_requested;
    static std::atomic<bool> s_http_requested;
    static std::atomic<bool> s_restart_requested;

#ifdef _WIN32
    static BOOL WINAPI console_ctrl_handler(DWORD ctrl_type);
#else
    static void signal_handler(int sig);
#endif
};

#endif
