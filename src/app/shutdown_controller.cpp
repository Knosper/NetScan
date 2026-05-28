#include "app/shutdown_controller.hpp"

#include <chrono>
#include "util/logger.hpp"

volatile sig_atomic_t ShutdownController::s_requested = 0;
std::atomic<bool> ShutdownController::s_http_requested{false};
std::atomic<bool> ShutdownController::s_restart_requested{false};

#ifdef _WIN32
BOOL WINAPI ShutdownController::console_ctrl_handler(DWORD ctrl_type)
{
    if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT ||
        ctrl_type == CTRL_CLOSE_EVENT || ctrl_type == CTRL_SHUTDOWN_EVENT)
    {
        s_requested = 1;
        return TRUE;
    }
    return FALSE;
}
#else
void ShutdownController::signal_handler(int /*sig*/)
{
    s_requested = 1;
}
#endif

bool ShutdownController::setup(Logger& logger)
{
    s_requested = 0;
    s_http_requested.store(false);
    s_restart_requested.store(false);
#ifdef _WIN32
    if (!SetConsoleCtrlHandler(console_ctrl_handler, TRUE))
    {
        logger.error("Failed to register Windows console control handler");
        return false;
    }
#else
    if (std::signal(SIGINT,  signal_handler) == SIG_ERR ||
        std::signal(SIGTERM, signal_handler) == SIG_ERR)
    {
        logger.error("Failed to register OS signal handlers");
        return false;
    }
#endif
    return true;
}

std::thread ShutdownController::start_watcher(std::function<void()> on_shutdown, Logger& logger)
{
    return std::thread([on_shutdown, &logger]()
    {
        while (!s_requested && !s_http_requested.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        logger.info("Shutdown signal received, stopping server...");
        on_shutdown();
    });
}

void ShutdownController::request()
{
    s_http_requested.store(true);
}

void ShutdownController::request_restart()
{
    s_restart_requested.store(true);
    s_http_requested.store(true);
}

bool ShutdownController::restart_requested()
{
    return s_restart_requested.load();
}
