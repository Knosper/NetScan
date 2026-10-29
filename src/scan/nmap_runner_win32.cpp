#include "scan/nmap_runner.hpp"

#include "scan/nmap_runner_internal.hpp"
#include "util/logger.hpp"
#include "util/windows_argument_quote.hpp"
#include "util/windows_executable_path.hpp"

#include <chrono>
#include <iterator>
#include <thread>
#include <vector>
#include <windows.h>

namespace
{
struct WindowsReadContext
{
    HANDLE read_pipe = nullptr;
    const PROCESS_INFORMATION* process_info = nullptr;
    HANDLE job = nullptr;
    NmapProcessListener* listener = nullptr;
    std::string* output = nullptr;
    NmapOutputWatch* watch = nullptr;
};

struct WindowsFinalizeContext
{
    const PROCESS_INFORMATION* process_info = nullptr;
    HANDLE job = nullptr;
    HANDLE stderr_handle = nullptr;
    NmapProcessListener* listener = nullptr;
    Logger* logger = nullptr;
    std::string* output = nullptr;
    std::string* stderr_text = nullptr;
    NmapOutputWatch* watch = nullptr;
};

std::string wide_to_utf8(const std::wstring& input)
{
    if (input.empty())
        return std::string();

    const int utf8_length =
        WideCharToMultiByte(CP_UTF8, 0, input.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (utf8_length <= 0)
        return std::string();

    std::vector<char> buffer(static_cast<std::vector<char>::size_type>(utf8_length));
    if (WideCharToMultiByte(CP_UTF8, 0, input.c_str(), -1, &buffer[0], utf8_length, nullptr,
                            nullptr) <= 0)
    {
        return std::string();
    }

    return std::string(&buffer[0]);
}

std::string format_windows_error(const char* action, DWORD error_code)
{
    LPWSTR message_buffer = nullptr;
    const DWORD format_result = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error_code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&message_buffer), 0, nullptr);

    std::string message;
    if (format_result == 0 || message_buffer == nullptr)
    {
        message = "unknown error";
    }
    else
    {
        std::wstring wide_message(message_buffer);
        while (!wide_message.empty() && (wide_message[wide_message.size() - 1] == L'\r' ||
                                         wide_message[wide_message.size() - 1] == L'\n' ||
                                         wide_message[wide_message.size() - 1] == L' '))
        {
            wide_message.erase(wide_message.size() - 1);
        }

        message = wide_to_utf8(wide_message);
        LocalFree(message_buffer);
    }

    return std::string(action) + ": " + message;
}


bool wait_for_process_exit(HANDLE process, DWORD timeout_ms, std::string* error)
{
    const DWORD wait_result = WaitForSingleObject(process, timeout_ms);
    if (wait_result == WAIT_OBJECT_0)
        return true;

    if (error)
    {
        if (wait_result == WAIT_TIMEOUT)
            *error = "timed out waiting for nmap process to exit";
        else
            *error = format_windows_error("failed to wait for nmap process", GetLastError());
    }
    return false;
}

bool is_process_still_active(HANDLE process)
{
    DWORD exit_code = 0;
    if (GetExitCodeProcess(process, &exit_code) == 0)
        return false;
    return exit_code == STILL_ACTIVE;
}

bool create_kill_on_close_job(HANDLE process, HANDLE& job, std::string& error)
{
    job = CreateJobObjectW(nullptr, nullptr);
    if (job == nullptr)
    {
        error = format_windows_error("failed to create crash cleanup job", GetLastError());
        return false;
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
    ZeroMemory(&limits, sizeof(limits));
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits,
                                sizeof(limits)) == 0)
    {
        error =
            format_windows_error("failed to configure crash cleanup job", GetLastError());
        CloseHandle(job);
        job = nullptr;
        return false;
    }

    if (AssignProcessToJobObject(job, process) == 0)
    {
        error = format_windows_error("failed to attach nmap process to crash cleanup job",
                                     GetLastError());
        CloseHandle(job);
        job = nullptr;
        return false;
    }

    return true;
}
} // namespace

// On Windows, graceful (SIGTERM) and forceful (SIGKILL) termination are equivalent:
// both use TerminateProcess. The distinction exists only on POSIX (SIGTERM vs. SIGKILL).
static bool terminate_nmap_process_impl(const NmapProcessHandle& handle, std::string* error)
{
    if (handle.process == nullptr)
        return true;

    if (!is_process_still_active(handle.process))
        return true;

    if (TerminateProcess(handle.process, 1) == 0)
    {
        const DWORD termination_error = GetLastError();
        if (termination_error == ERROR_ACCESS_DENIED && !is_process_still_active(handle.process))
            return true;

        if (error)
            *error = format_windows_error("failed to terminate nmap process", termination_error);
        return false;
    }

    return wait_for_process_exit(handle.process, 5000, error);
}

bool terminate_nmap_process(const NmapProcessHandle& handle, std::string* error)
{
    return terminate_nmap_process_impl(handle, error);
}

bool force_terminate_nmap_process(const NmapProcessHandle& handle, std::string* error)
{
    return terminate_nmap_process_impl(handle, error);
}

// Windows needs no reaping by the runner thread, so waiting for the exit is safe here.
bool kill_nmap_process_without_wait(const NmapProcessHandle& handle, std::string* error)
{
    return terminate_nmap_process_impl(handle, error);
}

namespace
{
SECURITY_ATTRIBUTES inheritable_security_attributes()
{
    SECURITY_ATTRIBUTES security_attributes;
    security_attributes.nLength = sizeof(security_attributes);
    security_attributes.lpSecurityDescriptor = nullptr;
    security_attributes.bInheritHandle = TRUE;
    return security_attributes;
}

bool create_output_pipe(HANDLE& read_pipe, HANDLE& write_pipe, std::string& error)
{
    SECURITY_ATTRIBUTES security_attributes = inheritable_security_attributes();
    read_pipe = nullptr;
    write_pipe = nullptr;
    if (CreatePipe(&read_pipe, &write_pipe, &security_attributes, 0) == 0)
    {
        error = format_windows_error("failed to create pipe", GetLastError());
        return false;
    }

    if (SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0) == 0)
    {
        error = format_windows_error("failed to configure pipe inheritance", GetLastError());
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        read_pipe = nullptr;
        write_pipe = nullptr;
        return false;
    }

    return true;
}

std::wstring get_windows_temp_dir()
{
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetTempPathW(MAX_PATH, buffer);
    if (length == 0 || length >= MAX_PATH)
        return std::wstring();
    return std::wstring(buffer, buffer + length);
}

bool create_stderr_file(HANDLE& stderr_handle, std::wstring& stderr_path, std::string& error)
{
    stderr_handle = nullptr;
    stderr_path.clear();

    const std::wstring temp_dir = get_windows_temp_dir();
    if (temp_dir.empty())
    {
        error = format_windows_error("failed to resolve Windows temp directory", GetLastError());
        return false;
    }

    std::vector<wchar_t> temp_path(temp_dir.begin(), temp_dir.end());
    temp_path.resize(MAX_PATH, L'\0');
    if (GetTempFileNameW(temp_dir.c_str(), L"netscan", 0, temp_path.data()) == 0)
    {
        error = format_windows_error("failed to allocate stderr temp file", GetLastError());
        return false;
    }

    stderr_path = temp_path.data();
    SECURITY_ATTRIBUTES security_attributes = inheritable_security_attributes();
    stderr_handle = CreateFileW(stderr_path.c_str(), GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                &security_attributes, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY,
                                nullptr);
    if (stderr_handle == INVALID_HANDLE_VALUE)
    {
        error = format_windows_error("failed to open stderr temp file", GetLastError());
        DeleteFileW(stderr_path.c_str());
        stderr_handle = nullptr;
        stderr_path.clear();
        return false;
    }

    return true;
}

bool open_null_input(HANDLE& input_handle, std::string& error)
{
    SECURITY_ATTRIBUTES security_attributes = inheritable_security_attributes();
    input_handle = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               &security_attributes, OPEN_EXISTING, 0, nullptr);
    if (input_handle == INVALID_HANDLE_VALUE)
    {
        input_handle = nullptr;
        error = format_windows_error("failed to open NUL for nmap stdin", GetLastError());
        return false;
    }
    return true;
}

std::wstring parent_directory(const std::wstring& path)
{
    const std::wstring::size_type separator = path.find_last_of(L"\\/");
    if (separator == std::wstring::npos)
        return std::wstring();
    // Keep the separator for a drive root ("C:\") so it does not turn into "C:".
    if (separator == 2 && path[1] == L':')
        return path.substr(0, 3);
    return path.substr(0, separator);
}

std::vector<wchar_t> build_mutable_command_line(const std::wstring& application,
                                                const ProcessSpec& process, std::string& error)
{
    std::vector<std::string> all_args;
    all_args.reserve(1 + process.args.size());
    all_args.push_back(wide_to_utf8(application));
    all_args.insert(all_args.end(), process.args.begin(), process.args.end());
    std::wstring command_line = util::build_windows_command_line(all_args);
    if (command_line.empty())
    {
        error = "failed to build Windows command line";
        return std::vector<wchar_t>();
    }

    std::vector<wchar_t> mutable_command_line(command_line.begin(), command_line.end());
    mutable_command_line.push_back(L'\0');
    return mutable_command_line;
}

struct ChildStdHandles
{
    HANDLE input = nullptr;
    HANDLE output = nullptr;
    HANDLE error = nullptr;
};

// Owns a PROC_THREAD_ATTRIBUTE_HANDLE_LIST so the child inherits only its own std handles,
// not the pipes of other scans that run in parallel or any other inheritable handle.
class InheritedHandleList
{
public:
    InheritedHandleList() = default;
    InheritedHandleList(const InheritedHandleList&) = delete;
    InheritedHandleList& operator=(const InheritedHandleList&) = delete;
    ~InheritedHandleList()
    {
        if (initialized_)
            DeleteProcThreadAttributeList(list());
    }

    bool initialize(const ChildStdHandles& handles, std::string& error)
    {
        handles_[0] = handles.input;
        handles_[1] = handles.output;
        handles_[2] = handles.error;

        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        buffer_.resize(size);
        if (size == 0 || InitializeProcThreadAttributeList(list(), 1, 0, &size) == 0)
        {
            error = format_windows_error("failed to initialize process attributes", GetLastError());
            return false;
        }
        initialized_ = true;

        if (UpdateProcThreadAttribute(list(), 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles_,
                                      sizeof(handles_), nullptr, nullptr) == 0)
        {
            error = format_windows_error("failed to restrict inherited handles", GetLastError());
            return false;
        }
        return true;
    }

    LPPROC_THREAD_ATTRIBUTE_LIST list()
    {
        return reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buffer_.data());
    }

private:
    std::vector<unsigned char> buffer_;
    HANDLE handles_[3] = {nullptr, nullptr, nullptr};
    bool initialized_ = false;
};

// Started suspended so the caller can put the process into its job before nmap runs.
// The working directory is nmap's own directory, so the child's DLL search never falls back to
// whatever directory NetScan was started from.
bool create_windows_process(const std::wstring& application, std::vector<wchar_t>& command_line,
                            const ChildStdHandles& handles, PROCESS_INFORMATION& process_info,
                            std::string& error)
{
    InheritedHandleList inherited_handles;
    if (!inherited_handles.initialize(handles, error))
        return false;

    STARTUPINFOEXW startup_info;
    ZeroMemory(&startup_info, sizeof(startup_info));
    startup_info.StartupInfo.cb = sizeof(startup_info);
    startup_info.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup_info.StartupInfo.hStdInput = handles.input;
    startup_info.StartupInfo.hStdOutput = handles.output;
    startup_info.StartupInfo.hStdError = handles.error;
    startup_info.lpAttributeList = inherited_handles.list();

    const std::wstring working_directory = parent_directory(application);
    ZeroMemory(&process_info, sizeof(process_info));
    if (CreateProcessW(application.c_str(), &command_line[0], nullptr, nullptr, TRUE,
                       EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED, nullptr,
                       working_directory.empty() ? nullptr : working_directory.c_str(),
                       &startup_info.StartupInfo, &process_info) == 0)
    {
        error = format_windows_error("failed to start nmap process", GetLastError());
        ZeroMemory(&process_info, sizeof(process_info));
        return false;
    }
    return true;
}

void cleanup_process_handles(const PROCESS_INFORMATION& process_info, HANDLE job)
{
    CloseHandle(process_info.hThread);
    CloseHandle(job);
    CloseHandle(process_info.hProcess);
}

NmapRunResult handle_windows_read_failure(const WindowsReadContext& context, DWORD error_code)
{
    const std::string error = format_windows_error("failed to read nmap output", error_code);
    CloseHandle(context.read_pipe);
    wait_for_process_exit(context.process_info->hProcess, INFINITE, nullptr);
    if (context.listener)
        context.listener->on_nmap_process_finished();
    cleanup_process_handles(*context.process_info, context.job);
    return {false, -1, *context.output, "", error};
}

NmapRunResult handle_windows_output_limit_exceeded(const WindowsReadContext& context)
{
    TerminateProcess(context.process_info->hProcess, 1);
    CloseHandle(context.read_pipe);
    wait_for_process_exit(context.process_info->hProcess, INFINITE, nullptr);
    if (context.listener)
        context.listener->on_nmap_process_finished();
    cleanup_process_handles(*context.process_info, context.job);
    return {false, -1, *context.output, "", "nmap output exceeded size limit"};
}

static const std::string::size_type MAX_NMAP_OUTPUT_BYTES = 64 * 1024 * 1024;

NmapRunResult read_windows_process_output(const WindowsReadContext& context)
{
    char buffer[4096];
    for (;;)
    {
        DWORD bytes_read = 0;
        if (ReadFile(context.read_pipe, buffer, sizeof(buffer), &bytes_read, nullptr) == 0)
        {
            const DWORD error_code = GetLastError();
            if (error_code == ERROR_BROKEN_PIPE)
                return {true, 0, "", "", ""};
            return handle_windows_read_failure(context, error_code);
        }

        if (bytes_read == 0)
            return {true, 0, "", "", ""};

        context.output->append(buffer, static_cast<std::string::size_type>(bytes_read));
        if (context.output->size() > MAX_NMAP_OUTPUT_BYTES)
            return handle_windows_output_limit_exceeded(context);
        consume_nmap_output_chunk(*context.watch, buffer, static_cast<std::string::size_type>(bytes_read));
    }
}

std::string read_windows_handle_text(HANDLE handle)
{
    if (handle == nullptr)
        return std::string();

    LARGE_INTEGER offset;
    offset.QuadPart = 0;
    if (SetFilePointerEx(handle, offset, nullptr, FILE_BEGIN) == 0)
        return std::string();

    std::string text;
    char buffer[4096];
    for (;;)
    {
        DWORD bytes_read = 0;
        if (ReadFile(handle, buffer, sizeof(buffer), &bytes_read, nullptr) == 0)
            return text;
        if (bytes_read == 0)
            return text;
        text.append(buffer, static_cast<std::string::size_type>(bytes_read));
    }
}

NmapRunResult finalize_windows_process(const WindowsFinalizeContext& context)
{
    std::string wait_error;
    if (!wait_for_process_exit(context.process_info->hProcess, INFINITE, &wait_error))
    {
        if (context.listener)
            context.listener->on_nmap_process_finished();
        if (context.stderr_handle != nullptr)
            CloseHandle(context.stderr_handle);
        cleanup_process_handles(*context.process_info, context.job);
        return {false, -1, *context.output, "", wait_error};
    }

    if (context.listener)
        context.listener->on_nmap_process_finished();

    *context.stderr_text = read_windows_handle_text(context.stderr_handle);
    if (context.stderr_handle != nullptr)
        CloseHandle(context.stderr_handle);
    if (context.logger != nullptr && !context.stderr_text->empty())
        context.logger->warn("nmap stderr: " + *context.stderr_text);

    DWORD exit_code = 0;
    if (GetExitCodeProcess(context.process_info->hProcess, &exit_code) == 0)
    {
        const std::string error =
            format_windows_error("failed to get nmap exit code", GetLastError());
        cleanup_process_handles(*context.process_info, context.job);
        return {false, -1, *context.output, *context.stderr_text, error};
    }

    cleanup_process_handles(*context.process_info, context.job);
    if (context.watch->detected_fatal_error)
        return {false, static_cast<int>(exit_code), *context.output, *context.stderr_text,
                context.watch->detected_error};
    if (exit_code != 0)
        return {false, static_cast<int>(exit_code), *context.output, *context.stderr_text,
                "nmap process exited with status " +
                    std::to_string(static_cast<unsigned long>(exit_code))};
    return {true, static_cast<int>(exit_code), *context.output, *context.stderr_text, ""};
}

struct ScopedHandle
{
    HANDLE h = nullptr;
    ScopedHandle() = default;
    explicit ScopedHandle(HANDLE h_) : h(h_) {}
    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;
    ScopedHandle(ScopedHandle&& o) noexcept : h(o.h) { o.h = nullptr; }
    ScopedHandle& operator=(ScopedHandle&& o) noexcept
    {
        if (h)
            CloseHandle(h);
        h = o.h;
        o.h = nullptr;
        return *this;
    }
    ~ScopedHandle() { if (h) CloseHandle(h); }
    HANDLE release() { HANDLE tmp = h; h = nullptr; return tmp; }
};

struct ScopedStderrFile
{
    std::wstring path;
    ScopedStderrFile() = default;
    ScopedStderrFile(const ScopedStderrFile&) = delete;
    ScopedStderrFile& operator=(const ScopedStderrFile&) = delete;
    ~ScopedStderrFile() { if (!path.empty()) DeleteFileW(path.c_str()); }
};

NmapRunResult abort_spawned_process(const PROCESS_INFORMATION& pi, const std::string& error)
{
    TerminateProcess(pi.hProcess, 1);
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return {false, -1, "", "", error};
}

} // namespace

NmapRunResult run_nmap_process(const ProcessSpec& process, Logger* logger,
                               NmapProcessListener* listener)
{
    if (process.executable.empty())
        return {false, -1, "", "", "process executable is empty"};
    const std::string xml_output_path = find_xml_output_path(process);

    HANDLE rp = nullptr;
    HANDLE swp = nullptr;
    std::string error;
    if (!create_output_pipe(rp, swp, error))
        return {false, -1, "", "", error};
    ScopedHandle read_pipe(rp);
    ScopedHandle stdout_write_pipe(swp);

    ScopedHandle stderr_handle;
    ScopedStderrFile stderr_file;
    if (!create_stderr_file(stderr_handle.h, stderr_file.path, error))
        return {false, -1, "", "", error};

    ScopedHandle stdin_handle;
    if (!open_null_input(stdin_handle.h, error))
        return {false, -1, "", "", error};

    std::wstring application;
    if (!util::resolve_windows_executable(process.executable,
                                          util::current_windows_search_path(), application, error))
        return {false, -1, "", "", error};

    std::vector<wchar_t> command_line = build_mutable_command_line(application, process, error);
    if (command_line.empty())
        return {false, -1, "", "", error};

    ChildStdHandles child_handles;
    child_handles.input = stdin_handle.h;
    child_handles.output = stdout_write_pipe.h;
    child_handles.error = stderr_handle.h;
    PROCESS_INFORMATION process_info;
    if (!create_windows_process(application, command_line, child_handles, process_info, error))
        return {false, -1, "", "", error};

    HANDLE job = nullptr;
    std::string job_error;
    if (!create_kill_on_close_job(process_info.hProcess, job, job_error))
        return abort_spawned_process(process_info, job_error);
    if (ResumeThread(process_info.hThread) == static_cast<DWORD>(-1))
    {
        const std::string resume_error =
            format_windows_error("failed to resume nmap process", GetLastError());
        CloseHandle(job);
        return abort_spawned_process(process_info, resume_error);
    }

    stdout_write_pipe = ScopedHandle{};
    stdin_handle = ScopedHandle{};
    if (logger)
        logger->info("nmap process started (pid " +
                     std::to_string(static_cast<unsigned long>(GetProcessId(process_info.hProcess))) +
                     ")");
    if (listener)
        listener->on_nmap_process_started({process_info.hProcess, job});

    std::string output;
    std::string stderr_text;
    NmapOutputWatch watch;
    OutputWatchConfig watch_config;
    watch_config.process = &process;
    watch_config.logger = logger;
    watch_config.listener = listener;
    initialize_output_watch(watch, watch_config);
    WindowsReadContext read_context;
    read_context.read_pipe = read_pipe.release();
    read_context.process_info = &process_info;
    read_context.job = job;
    read_context.listener = listener;
    read_context.output = &output;
    read_context.watch = &watch;
    const NmapRunResult read_result = read_windows_process_output(read_context);
    if (!read_result.ok && !read_result.error.empty())
        return read_result;

    finish_nmap_output_watch(watch);
    CloseHandle(read_context.read_pipe);
    WindowsFinalizeContext finalize_context;
    finalize_context.process_info = &process_info;
    finalize_context.job = job;
    finalize_context.stderr_handle = stderr_handle.release();
    finalize_context.listener = listener;
    finalize_context.logger = logger;
    finalize_context.output = &output;
    finalize_context.stderr_text = &stderr_text;
    finalize_context.watch = &watch;
    const NmapRunResult result = finalize_windows_process(finalize_context);
    const std::string xml_output = read_text_file(xml_output_path);
    if (!xml_output_path.empty())
        DeleteFileA(xml_output_path.c_str());

    if (!result.ok)
        return {false, result.exit_code, xml_output.empty() ? result.output : xml_output,
                result.stderr_text, result.error};

    return {true, result.exit_code, xml_output.empty() ? result.output : xml_output,
            result.stderr_text, ""};
}
