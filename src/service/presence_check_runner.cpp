#include "service/presence_check_runner.hpp"
#include "scan/target_validation.hpp"
#include "util/logger.hpp"
#include "util/windows_argument_quote.hpp"

#include "httplib/httplib.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <chrono>
#include <string>
#include <vector>

namespace
{
#ifdef _WIN32
typedef SOCKET socket_handle;
const socket_handle INVALID_SOCKET_HANDLE = INVALID_SOCKET;
#else
typedef int socket_handle;
const socket_handle INVALID_SOCKET_HANDLE = -1;
#endif

void close_socket(socket_handle socket_fd)
{
#ifdef _WIN32
    closesocket(socket_fd);
#else
    close(socket_fd);
#endif
}

class SocketRuntime
{
public:
    SocketRuntime() : ok_(true)
    {
#ifdef _WIN32
        WSADATA data;
        ok_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
#endif
    }

    ~SocketRuntime()
    {
#ifdef _WIN32
        if (ok_)
            WSACleanup();
#endif
    }

    bool ok() const
    {
        return ok_;
    }

private:
    bool ok_;
};

constexpr const char* PRESENCE_STATUS_UP      = "up";
constexpr const char* PRESENCE_STATUS_DOWN    = "down";
constexpr const char* PRESENCE_STATUS_TIMEOUT = "timeout";
constexpr const char* PRESENCE_STATUS_ERROR   = "error";

constexpr const char* CHECK_TYPE_PING = "ping";
constexpr const char* CHECK_TYPE_TCP  = "tcp";
constexpr const char* CHECK_TYPE_HTTP = "http";

constexpr int PING_TERMINATION_POLL_ATTEMPTS = 50;

struct PresenceCheckSpec
{
    std::string status;
    int latency_ms = -1;
    std::string error;
};

struct TcpEndpoint
{
    std::string host;
    int port = 0;
    int timeout_ms = 0;
};

struct PingCommand
{
    std::vector<std::string> args;
    int timeout_threshold_ms = 0;
};

struct PingExecution
{
    int rc = -1;
    int elapsed_ms = 0;
    int timeout_threshold_ms = 0;
};

bool is_safe_ping_target(const std::string& target)
{
    if (target.empty())
        return false;

    if (target[0] == '-')
        return false;

    for (char c : target)
    {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' ||
            c == '_' || c == ':')
            continue;
        return false;
    }
    return true;
}

int elapsed_ms(std::chrono::steady_clock::time_point start)
{
    const auto elapsed = std::chrono::steady_clock::now() - start;
    return static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

PresenceCheckResult make_result(const PresenceTracker& tracker, const PresenceCheckSpec& spec)
{
    PresenceCheckResult result;
    result.tracker_id = tracker.id;
    result.status = spec.status;
    result.latency_ms = spec.latency_ms;
    result.error = spec.error;
    return result;
}

#ifdef _WIN32
STARTUPINFOW hidden_startup_info()
{
    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    return si;
}

int execute_ping_command(const std::vector<std::string>& args, int timeout_ms, Logger* logger)
{
    std::wstring cmdline = util::build_windows_command_line(args);
    std::vector<wchar_t> command_buffer(cmdline.begin(), cmdline.end());
    command_buffer.push_back(L'\0');

    STARTUPINFOW si = hidden_startup_info();
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessW(nullptr, &command_buffer[0], nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return -1;

    DWORD wait_result = WaitForSingleObject(
        pi.hProcess, static_cast<DWORD>(std::max(1, timeout_ms)));
    if (wait_result == WAIT_TIMEOUT)
    {
        if (logger)
            logger->debug("presence ping process exceeded timeout; terminating process");
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 1000);
    }

    DWORD exit_code = 1;
    GetExitCodeProcess(pi.hProcess, &exit_code);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return static_cast<int>(exit_code);
}
#else
void redirect_child_stdio_to_devnull()
{
    int devnull = open("/dev/null", O_RDWR);
    if (devnull < 0)
        return;

    dup2(devnull, STDOUT_FILENO);
    dup2(devnull, STDERR_FILENO);
    close(devnull);
}

std::vector<const char*> exec_argv(const std::vector<std::string>& args)
{
    std::vector<const char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& arg : args)
        argv.push_back(arg.c_str());
    argv.push_back(nullptr);
    return argv;
}

bool wait_for_ping_exit(pid_t pid, int options, int& rc)
{
    int status = 0;
    const pid_t result = waitpid(pid, &status, options);
    if (result == 0)
        return false;
    if (result != pid)
    {
        rc = -1;
        return true;
    }
    if (WIFEXITED(status))
        rc = WEXITSTATUS(status);
    else
        rc = -1;
    return true;
}

bool poll_ping_exit(pid_t pid, int attempts, int& rc)
{
    for (int i = 0; i < attempts; ++i)
    {
        if (wait_for_ping_exit(pid, WNOHANG, rc))
            return true;
        usleep(20000);
    }
    return false;
}

int wait_for_ping_exit_with_timeout(pid_t pid, int timeout_ms, Logger* logger)
{
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(std::max(1, timeout_ms));
    int rc = -1;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (wait_for_ping_exit(pid, WNOHANG, rc))
            return rc;
        usleep(20000);
    }

    if (logger)
        logger->debug("presence ping process exceeded timeout; terminating process");
    kill(pid, SIGTERM);
    if (poll_ping_exit(pid, PING_TERMINATION_POLL_ATTEMPTS, rc))
        return rc;

    kill(pid, SIGKILL);
    poll_ping_exit(pid, PING_TERMINATION_POLL_ATTEMPTS, rc);
    return -1;
}

int execute_ping_command(const std::vector<std::string>& args, int timeout_ms, Logger* logger)
{
    pid_t pid = fork();
    if (pid < 0)
        return -1;

    if (pid == 0)
    {
        redirect_child_stdio_to_devnull();
        std::vector<const char*> argv = exec_argv(args);
        execvp(argv[0], const_cast<char* const*>(argv.data()));
        _exit(127);
    }

    return wait_for_ping_exit_with_timeout(pid, timeout_ms, logger);
}
#endif

bool set_nonblocking(socket_handle socket_fd)
{
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(socket_fd, FIONBIO, &mode) == 0;
#else
    const int flags = fcntl(socket_fd, F_GETFL, 0);
    return flags >= 0 && fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

bool socket_connected(socket_handle socket_fd, int timeout_ms, bool& timed_out)
{
    fd_set write_set;
    FD_ZERO(&write_set);
    FD_SET(socket_fd, &write_set);

    timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;

    const int rc = select(static_cast<int>(socket_fd + 1), nullptr, &write_set, nullptr, &timeout);
    timed_out = rc == 0;
    if (rc <= 0)
        return false;

    int error = 0;
#ifdef _WIN32
    int error_len = sizeof(error);
#else
    socklen_t error_len = sizeof(error);
#endif
    if (getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error),
                   &error_len) != 0)
        return false;
    return error == 0;
}

bool try_connect_address(addrinfo* addr, int timeout_ms, bool& timed_out)
{
    timed_out = false;
    socket_handle socket_fd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
    if (socket_fd == INVALID_SOCKET_HANDLE)
        return false;

    if (!set_nonblocking(socket_fd))
    {
        close_socket(socket_fd);
        return false;
    }

    const int rc = connect(socket_fd, addr->ai_addr, static_cast<int>(addr->ai_addrlen));
    if (rc == 0)
    {
        close_socket(socket_fd);
        return true;
    }

#ifdef _WIN32
    const int error = WSAGetLastError();
    if (error != WSAEWOULDBLOCK && error != WSAEINPROGRESS)
#else
    if (errno != EINPROGRESS)
#endif
    {
        close_socket(socket_fd);
        return false;
    }

    const bool connected = socket_connected(socket_fd, timeout_ms, timed_out);
    close_socket(socket_fd);
    return connected;
}

bool resolve_tcp_endpoint(const TcpEndpoint& endpoint, addrinfo*& addresses)
{
    addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    const std::string port_text = std::to_string(endpoint.port);
    if (getaddrinfo(endpoint.host.c_str(), port_text.c_str(), &hints, &addresses) != 0)
        return false;
    return true;
}

bool connect_tcp_addresses(addrinfo* addresses, int timeout_ms, bool& timed_out)
{
    bool saw_timeout = false;
    bool connected = false;
    for (addrinfo* addr = addresses; addr != nullptr; addr = addr->ai_next)
    {
        bool address_timed_out = false;
        if (try_connect_address(addr, timeout_ms, address_timed_out))
        {
            connected = true;
            break;
        }
        saw_timeout = saw_timeout || address_timed_out;
    }
    timed_out = saw_timeout && !connected;
    return connected;
}

bool connect_tcp(const TcpEndpoint& endpoint, bool& timed_out)
{
    SocketRuntime runtime;
    if (!runtime.ok())
        return false;

    addrinfo* addresses = nullptr;
    if (!resolve_tcp_endpoint(endpoint, addresses))
        return false;

    const bool connected = connect_tcp_addresses(addresses, endpoint.timeout_ms, timed_out);
    freeaddrinfo(addresses);
    return connected;
}

PingCommand ping_command_for(const PresenceTracker& tracker)
{
    PingCommand command;
    command.timeout_threshold_ms = tracker.timeout_ms;
#ifdef _WIN32
    command.args = {"ping", "-n", "1", "-w", std::to_string(tracker.timeout_ms), tracker.target};
#else
    const int timeout_seconds = std::max(1, (tracker.timeout_ms + 999) / 1000);
    command.args = {"ping", "-c", "1", "-W", std::to_string(timeout_seconds), tracker.target};
#endif
    return command;
}

PresenceCheckResult evaluate_ping_result(const PresenceTracker& tracker,
                                         const PingExecution& execution)
{
    if (execution.rc == 0)
        return make_result(tracker, PresenceCheckSpec{PRESENCE_STATUS_UP, execution.elapsed_ms, ""});
    if (execution.elapsed_ms >= execution.timeout_threshold_ms)
        return make_result(tracker, PresenceCheckSpec{PRESENCE_STATUS_TIMEOUT, -1, "ping timed out"});
    return make_result(tracker, PresenceCheckSpec{PRESENCE_STATUS_DOWN, -1, "ping failed"});
}

PresenceCheckResult run_ping_check(const PresenceTracker& tracker, Logger* logger)
{
    if (!is_safe_ping_target(tracker.target))
        return make_result(tracker, PresenceCheckSpec{PRESENCE_STATUS_ERROR, -1, "invalid ping target"});

    const auto start = std::chrono::steady_clock::now();
    const PingCommand command = ping_command_for(tracker);
    PingExecution execution;
    execution.rc = execute_ping_command(command.args, command.timeout_threshold_ms, logger);
    execution.elapsed_ms = elapsed_ms(start);
    execution.timeout_threshold_ms = command.timeout_threshold_ms;
    return evaluate_ping_result(tracker, execution);
}

PresenceCheckResult run_tcp_check(const PresenceTracker& tracker)
{
    const auto start = std::chrono::steady_clock::now();
    bool timed_out = false;
    const TcpEndpoint endpoint = {tracker.target, tracker.port, tracker.timeout_ms};
    if (connect_tcp(endpoint, timed_out))
        return make_result(tracker, PresenceCheckSpec{PRESENCE_STATUS_UP, elapsed_ms(start), ""});
    if (timed_out)
        return make_result(tracker, PresenceCheckSpec{PRESENCE_STATUS_TIMEOUT, -1, "tcp connect timed out"});
    return make_result(tracker, PresenceCheckSpec{PRESENCE_STATUS_DOWN, -1, "tcp connect failed"});
}

PresenceCheckResult run_http_check(const PresenceTracker& tracker)
{
    HttpTarget http_target;
    std::string error;
    if (!parse_http_url(tracker, http_target, error))
        return make_result(tracker, PresenceCheckSpec{PRESENCE_STATUS_ERROR, -1, error});

    httplib::Client client(http_target.host, http_target.port);
    client.set_connection_timeout(0, tracker.timeout_ms * 1000);
    client.set_read_timeout(0, tracker.timeout_ms * 1000);

    const auto start = std::chrono::steady_clock::now();
    auto res = client.Get(http_target.path.c_str());
    if (!res)
    {
        const bool timed_out = res.error() == httplib::Error::ConnectionTimeout ||
                               res.error() == httplib::Error::Read;
        if (timed_out)
            return make_result(tracker, PresenceCheckSpec{PRESENCE_STATUS_TIMEOUT, -1, "http connect timed out"});
        return make_result(tracker, PresenceCheckSpec{PRESENCE_STATUS_DOWN, -1, "http connect failed"});
    }
    if (res->status >= 100 && res->status < 500)
        return make_result(tracker, PresenceCheckSpec{PRESENCE_STATUS_UP, elapsed_ms(start), ""});
    return make_result(tracker,
        PresenceCheckSpec{PRESENCE_STATUS_DOWN, elapsed_ms(start),
                          "http status " + std::to_string(res->status)});
}
} // namespace

bool parse_http_url(const PresenceTracker& tracker, HttpTarget& target, std::string& error)
{
    const std::string url = tracker.url.empty() ? std::string("http://") + tracker.target
                                                : tracker.url;
    const std::string prefix = "http://";
    if (url.compare(0, prefix.size(), prefix) != 0)
    {
        error = "http checks support http:// URLs only";
        return false;
    }

    const std::string rest = url.substr(prefix.size());
    const std::string::size_type slash = rest.find('/');
    const std::string authority = rest.substr(0, slash);
    target.path = slash == std::string::npos ? "/" : rest.substr(slash);
    const std::string::size_type colon = authority.rfind(':');
    target.host = colon == std::string::npos ? authority : authority.substr(0, colon);

    if (target.host.empty())
    {
        error = "http URL must include a host";
        return false;
    }

    if (colon != std::string::npos)
    {
        if (!parse_canonical_uint_in_range(authority.substr(colon + 1), 1, 65535, target.port))
        {
            error = "http URL port must be between 1 and 65535";
            return false;
        }
    }
    else if (tracker.port > 0)
    {
        target.port = tracker.port;
    }
    return true;
}

DefaultPresenceCheckRunner::DefaultPresenceCheckRunner(Logger* logger) : logger_(logger)
{
}

PresenceCheckResult DefaultPresenceCheckRunner::run(const PresenceTracker& tracker)
{
    if (tracker.check_type == CHECK_TYPE_PING)
        return run_ping_check(tracker, logger_);
    if (tracker.check_type == CHECK_TYPE_TCP)
        return run_tcp_check(tracker);
    if (tracker.check_type == CHECK_TYPE_HTTP)
        return run_http_check(tracker);

    return make_result(tracker, PresenceCheckSpec{PRESENCE_STATUS_ERROR, -1, "unsupported check type"});
}
