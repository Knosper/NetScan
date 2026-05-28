#include "scan/nmap_runner.hpp"

#include "scan/nmap_runner_internal.hpp"
#include "util/logger.hpp"

#include <chrono>
#include <thread>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <sstream>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/prctl.h>

namespace
{
struct PosixReadContext
{
    int read_fd = -1;
    pid_t pid = -1;
    NmapProcessListener* listener = nullptr;
    std::shared_ptr<NmapProcessLifecycle> lifecycle;
    std::string* output = nullptr;
    NmapOutputWatch* watch = nullptr;
    int* status = nullptr;
};


struct PosixSignalRequest
{
    const NmapProcessHandle* handle = nullptr;
    int signal = 0;
    const char* action = nullptr;
    std::string* error = nullptr;
};

std::string errno_message(const char* action)
{
    return std::string(action) + ": " + std::strerror(errno);
}

bool is_process_group_alive(pid_t process_group_id)
{
    if (process_group_id <= 0)
        return false;

    if (kill(-process_group_id, 0) == 0)
        return true;

    return errno == EPERM;
}

bool wait_for_process_group_exit(pid_t process_group_id, int timeout_ms)
{
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < deadline)
    {
        if (!is_process_group_alive(process_group_id))
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    return !is_process_group_alive(process_group_id);
}

bool is_handle_finished_locked(const NmapProcessHandle& handle)
{
    return handle.lifecycle && handle.lifecycle->finished;
}

bool is_handle_finished(const NmapProcessHandle& handle)
{
    if (!handle.lifecycle)
        return false;

    std::lock_guard<std::mutex> lock(handle.lifecycle->mutex);
    return is_handle_finished_locked(handle);
}

bool send_signal_to_live_process_group(const PosixSignalRequest& request)
{
    std::unique_lock<std::mutex> lock;
    if (request.handle->lifecycle)
        lock = std::unique_lock<std::mutex>(request.handle->lifecycle->mutex);

    if (is_handle_finished_locked(*request.handle))
        return true;

    if (kill(-request.handle->process_group_id, request.signal) == 0 || errno == ESRCH)
        return true;

    if (request.error)
        *request.error = errno_message(request.action);
    return false;
}

void mark_process_finished(const std::shared_ptr<NmapProcessLifecycle>& lifecycle)
{
    if (!lifecycle)
        return;

    std::lock_guard<std::mutex> lock(lifecycle->mutex);
    lifecycle->finished = true;
}

bool wait_for_child_exit_without_reaping(pid_t pid, std::string* error)
{
    siginfo_t child_info;
    std::memset(&child_info, 0, sizeof(child_info));

    while (waitid(P_PID, pid, &child_info, WEXITED | WNOWAIT) != 0)
    {
        if (errno == EINTR)
            continue;
        if (error)
            *error = errno_message("failed to wait for nmap process");
        return false;
    }

    return true;
}

bool reap_child_process(pid_t pid, int& status, std::string* error)
{
    while (waitpid(pid, &status, 0) == -1)
    {
        if (errno == EINTR)
            continue;
        if (error)
            *error = errno_message("failed to wait for nmap process");
        return false;
    }

    return true;
}
} // namespace

bool terminate_nmap_process(const NmapProcessHandle& handle, std::string* error)
{
    if (handle.process_group_id <= 0)
        return true;

    PosixSignalRequest request;
    request.handle = &handle;
    request.signal = SIGTERM;
    request.action = "failed to send SIGTERM to nmap process group";
    request.error = error;
    if (!send_signal_to_live_process_group(request))
        return false;

    if (wait_for_process_group_exit(handle.process_group_id, 1000))
        return true;
    if (is_handle_finished(handle))
        return true;

    request.signal = SIGKILL;
    request.action = "failed to send SIGKILL to nmap process group";
    if (!send_signal_to_live_process_group(request))
        return false;

    if (wait_for_process_group_exit(handle.process_group_id, 1000))
        return true;
    if (is_handle_finished(handle))
        return true;

    if (error)
        *error = "nmap process group did not terminate";
    return false;
}

bool force_terminate_nmap_process(const NmapProcessHandle& handle, std::string* error)
{
    if (handle.process_group_id <= 0)
        return true;

    PosixSignalRequest request;
    request.handle = &handle;
    request.signal = SIGKILL;
    request.action = "failed to send SIGKILL to nmap process group";
    request.error = error;
    if (!send_signal_to_live_process_group(request))
        return false;

    if (wait_for_process_group_exit(handle.process_group_id, 1000))
        return true;
    if (is_handle_finished(handle))
        return true;

    if (error)
        *error = "nmap process group did not terminate after SIGKILL";
    return false;
}

namespace
{
std::vector<char*> build_posix_argv(const ProcessSpec& process)
{
    std::vector<char*> argv;
    argv.reserve(process.args.size() + 2);
    argv.push_back(const_cast<char*>(process.executable.c_str()));
    for (std::vector<std::string>::const_iterator it = process.args.begin();
         it != process.args.end(); ++it)
    {
        argv.push_back(const_cast<char*>(it->c_str()));
    }
    argv.push_back(nullptr);
    return argv;
}

void ensure_process_group_created(pid_t pid)
{
    if (pid <= 0)
        return;

    if (setpgid(pid, pid) == 0)
        return;

    if (errno == EACCES || errno == ESRCH)
        return;
}

void exec_child_process_with_pipe(const ProcessSpec& process, int pipefd[2])
{
    if (setpgid(0, 0) != 0)
        _exit(127);

#ifdef __linux__
    if (prctl(PR_SET_PDEATHSIG, SIGTERM) != 0)
        _exit(127);
    if (getppid() == 1)
        _exit(127);
#endif

    if (dup2(pipefd[1], STDOUT_FILENO) == -1 || dup2(pipefd[1], STDERR_FILENO) == -1)
        _exit(127);

    close(pipefd[0]);
    close(pipefd[1]);

    std::vector<char*> argv = build_posix_argv(process);
    execvp(process.executable.c_str(), argv.data());
    _exit(127);
}

NmapRunResult handle_posix_read_failure(const PosixReadContext& context, int read_errno)
{
    std::string wait_error;
    if (wait_for_child_exit_without_reaping(context.pid, &wait_error))
        mark_process_finished(context.lifecycle);
    reap_child_process(context.pid, *context.status, nullptr);
    if (context.listener)
        context.listener->on_nmap_process_finished();
    finish_nmap_output_watch(*context.watch);
    const int exit_code = WIFEXITED(*context.status) != 0 ? WEXITSTATUS(*context.status) : -1;
    errno = read_errno;
    return {false, exit_code, *context.output, "",
            errno_message("failed to read nmap output")};
}

NmapRunResult handle_posix_output_limit_exceeded(const PosixReadContext& context)
{
    kill(-context.pid, SIGKILL);
    std::string wait_error;
    if (wait_for_child_exit_without_reaping(context.pid, &wait_error))
        mark_process_finished(context.lifecycle);
    reap_child_process(context.pid, *context.status, nullptr);
    if (context.listener)
        context.listener->on_nmap_process_finished();
    finish_nmap_output_watch(*context.watch);
    return {false, -1, *context.output, "", "nmap output exceeded size limit"};
}

static const std::string::size_type MAX_NMAP_OUTPUT_BYTES = 64 * 1024 * 1024;

NmapRunResult read_posix_process_output(const PosixReadContext& context)
{
    char buffer[4096];
    for (;;)
    {
        const ssize_t n = read(context.read_fd, buffer, sizeof(buffer));
        if (n > 0)
        {
            context.output->append(buffer, static_cast<std::string::size_type>(n));
            if (context.output->size() > MAX_NMAP_OUTPUT_BYTES)
                return handle_posix_output_limit_exceeded(context);
            consume_nmap_output_chunk(*context.watch, buffer, static_cast<std::string::size_type>(n));
            continue;
        }
        if (n == 0)
        {
            return {true, 0, "", "", ""};
        }
#ifdef __linux__
        if (errno == EIO)
        {
            return {true, 0, "", "", ""};
        }
#endif
        if (errno == EINTR)
            continue;

        return handle_posix_read_failure(context, errno);
    }
}


bool create_posix_pipe(int pipefd[2], NmapRunResult& error_result)
{
    if (pipe(pipefd) == 0)
        return true;

    error_result = {false, -1, "", "", errno_message("failed to create pipe")};
    return false;
}

pid_t fork_posix_process(int pipefd[2], NmapRunResult& error_result)
{
    const pid_t pid = fork();
    if (pid >= 0)
        return pid;

    close(pipefd[0]);
    close(pipefd[1]);
    error_result = {false, -1, "", "", errno_message("failed to fork process")};
    return -1;
}

NmapRunResult interpret_posix_exit_status(int status, const std::string& output,
                                           const NmapOutputWatch& watch)
{
    if (WIFEXITED(status) != 0)
    {
        const int exit_code = WEXITSTATUS(status);
        if (watch.detected_fatal_error)
            return {false, exit_code, output, "", watch.detected_error};
        return {true, exit_code, output, "", ""};
    }

    if (WIFSIGNALED(status) != 0)
    {
        const int sig = WTERMSIG(status);
        std::ostringstream msg;
        msg << "nmap process killed by signal " << sig;
        const char* name = strsignal(sig);
        if (name)
            msg << " (" << name << ")";
        return {false, -1, output, "", msg.str()};
    }

    return {false, -1, output, "", "nmap process terminated unexpectedly"};
}

} // namespace

NmapRunResult run_nmap_process(const ProcessSpec& process, Logger* logger,
                               NmapProcessListener* listener)
{
    if (process.executable.empty())
        return {false, -1, "", "", "process executable is empty"};
    const std::string xml_output_path = find_xml_output_path(process);

    int pipefd[2];
    NmapRunResult setup_error;
    if (!create_posix_pipe(pipefd, setup_error))
        return setup_error;

    const pid_t pid = fork_posix_process(pipefd, setup_error);
    if (pid < 0)
        return setup_error;

    if (pid == 0)
        exec_child_process_with_pipe(process, pipefd);

    ensure_process_group_created(pid);
    close(pipefd[1]);
    if (logger)
        logger->info("nmap process started (pid " + std::to_string(pid) + ")");
    const std::shared_ptr<NmapProcessLifecycle> lifecycle(new NmapProcessLifecycle());
    if (listener)
        listener->on_nmap_process_started({pid, pid, lifecycle});

    std::string output;
    NmapOutputWatch watch;
    OutputWatchConfig watch_config;
    watch_config.process = &process;
    watch_config.logger = logger;
    watch_config.listener = listener;
    initialize_output_watch(watch, watch_config);
    int status = 0;
    PosixReadContext read_context;
    read_context.read_fd = pipefd[0];
    read_context.pid = pid;
    read_context.listener = listener;
    read_context.lifecycle = lifecycle;
    read_context.output = &output;
    read_context.watch = &watch;
    read_context.status = &status;
    const NmapRunResult read_result = read_posix_process_output(read_context);
    close(pipefd[0]);
    if (!read_result.ok && !read_result.error.empty())
        return read_result;

    finish_nmap_output_watch(watch);
    std::string wait_error;
    if (!wait_for_child_exit_without_reaping(pid, &wait_error))
        return {false, -1, output, "", wait_error};
    mark_process_finished(lifecycle);
    if (listener)
        listener->on_nmap_process_finished();
    if (!reap_child_process(pid, status, &wait_error))
        return {false, -1, output, "", wait_error};

    const NmapRunResult wait_result = interpret_posix_exit_status(status, output, watch);
    const std::string xml_output = read_text_file(xml_output_path);
    if (!xml_output_path.empty())
        unlink(xml_output_path.c_str());

    if (!wait_result.ok)
        return {false, wait_result.exit_code, xml_output.empty() ? output : xml_output, "",
                wait_result.error};

    return {true, wait_result.exit_code, xml_output.empty() ? output : xml_output, "", ""};
}
