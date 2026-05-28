#include "scan/scan_orchestrator.hpp"

#include <cstdio>
#include <string>
#include <vector>

#include "scan/nmap_command_builder.hpp"
#include "scan/nmap_runner.hpp"
#include "scan/process_renderer.hpp"
#include "util/logger.hpp"

#ifdef __linux__
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace
{
constexpr char LOG_REQUEST_REJECTED_PREFIX[] = "scan orchestration: request rejected: ";
constexpr char LOG_RUNNING_COMMAND_PREFIX[] = "scan orchestration: running command: ";
constexpr char LOG_SCAN_EXECUTION_FAILED_PREFIX[] = "scan orchestration: scan execution failed: ";
constexpr char LOG_NMAP_NON_ZERO_EXIT_PREFIX[] = "scan orchestration: nmap exited with code ";
constexpr char LOG_SCAN_COMPLETED[] = "scan orchestration: scan completed successfully";

constexpr char MSG_SCAN_COMPLETED[] = "scan completed";
constexpr char MSG_SCAN_NON_ZERO_EXIT[] = "nmap exited with non-zero status";

struct ScanResultArgs
{
    ScanOutcome outcome;
    std::string message;
    std::string command;
    int exit_code = -1;
    std::string output;
    std::string stderr_text;
};

struct ExecuteScanContext
{
    const ProcessSpec& process;
    const std::string& rendered_command;
    NmapProcessListener* listener;
};

ScanResult make_result(const ScanResultArgs& args)
{
    ScanResult r;
    r.outcome = args.outcome;
    r.message = args.message;
    r.command = args.command;
    r.exit_code = args.exit_code;
    r.raw_output = args.output;
    r.stderr_text = args.stderr_text;
    return r;
}

std::string diagnostic_text(const NmapRunResult& run_result)
{
    if (!run_result.stderr_text.empty())
        return run_result.stderr_text;
    if (!run_result.error.empty())
        return run_result.error;
    return run_result.output;
}

NmapCommandBuildResult build_scan_command(const ScanRequest& request, Logger& logger)
{
    NmapCommandBuildResult result = build_nmap_command(request);
    if (!result.ok)
        logger.warn(std::string(LOG_REQUEST_REJECTED_PREFIX) + result.error);
    return result;
}

ScanResult execute_scan_command(ExecuteScanContext context, Logger& logger)
{
    NmapRunResult run_result = run_nmap_process(context.process, &logger, context.listener);

    if (!run_result.ok)
    {
        logger.error(std::string(LOG_SCAN_EXECUTION_FAILED_PREFIX) + run_result.error);
        return make_result({ScanOutcome::Failed, run_result.error, context.rendered_command,
                            run_result.exit_code, run_result.output, diagnostic_text(run_result)});
    }

    if (run_result.exit_code != 0)
    {
        logger.error(std::string(LOG_NMAP_NON_ZERO_EXIT_PREFIX) +
                     std::to_string(run_result.exit_code));
        return make_result({ScanOutcome::Failed, MSG_SCAN_NON_ZERO_EXIT, context.rendered_command,
                            run_result.exit_code, run_result.output, diagnostic_text(run_result)});
    }

    logger.info(LOG_SCAN_COMPLETED);
    return make_result({ScanOutcome::Completed, MSG_SCAN_COMPLETED, context.rendered_command,
                        run_result.exit_code, run_result.output, run_result.stderr_text});
}

#if defined(__linux__) || defined(_WIN32)
#ifdef __linux__
bool is_usable_runtime_dir(const std::string& path)
{
    if (path.empty())
        return false;

    struct stat st;
    if (stat(path.c_str(), &st) != 0)
        return false;
    if (!S_ISDIR(st.st_mode))
        return false;
    return access(path.c_str(), W_OK | X_OK) == 0;
}

std::string preferred_xml_temp_dir()
{
    const char* xdg_runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    if (xdg_runtime_dir != nullptr && is_usable_runtime_dir(xdg_runtime_dir))
        return xdg_runtime_dir;

    const std::string run_user_dir = std::string("/run/user/") + std::to_string(getuid());
    if (is_usable_runtime_dir(run_user_dir))
        return run_user_dir;

    return "/tmp";
}
#else
std::string preferred_xml_temp_dir()
{
    char temp_dir[MAX_PATH];
    const DWORD len = GetTempPathA(MAX_PATH, temp_dir);
    if (len == 0 || len >= MAX_PATH)
        return ".";
    return std::string(temp_dir, len);
}
#endif

bool rewrite_xml_stdout_to_temp_file(ProcessSpec& process, Logger& logger)
{
    for (std::vector<std::string>::size_type i = 0; i + 1 < process.args.size(); ++i)
    {
        if (process.args[i] != "-oX" || process.args[i + 1] != "-")
            continue;

#ifdef __linux__
        std::string path_template = preferred_xml_temp_dir() + "/netscan-nmap-XXXXXX.xml";
        std::vector<char> mutable_path(path_template.begin(), path_template.end());
        mutable_path.push_back('\0');
        const int fd = mkstemps(mutable_path.data(), 4);
        if (fd < 0)
        {
            logger.error("scan orchestration: failed to create temporary nmap XML output file");
            return false;
        }

        close(fd);
        process.args[i + 1] = mutable_path.data();
#else
        char temp_file[MAX_PATH];
        if (GetTempFileNameA(preferred_xml_temp_dir().c_str(), "netscan", 0, temp_file) == 0)
        {
            logger.error("scan orchestration: failed to create temporary nmap XML output file");
            return false;
        }
        process.args[i + 1] = temp_file;
#endif
        return true;
    }

    return true;
}
#endif
} // namespace

ScanOrchestrator::ScanOrchestrator(Logger& logger, NmapProcessListener* listener)
    : logger_(logger), listener_(listener)
{
}

ScanResult ScanOrchestrator::run_sync(const ScanRequest& request) const
{
    NmapCommandBuildResult build_result = build_scan_command(request, logger_);
    if (!build_result.ok)
        return make_result({ScanOutcome::Rejected, build_result.error, "", -1, "", ""});

    ProcessSpec process = build_result.process;
#if defined(__linux__) || defined(_WIN32)
    if (!rewrite_xml_stdout_to_temp_file(process, logger_))
        return make_result({ScanOutcome::Failed, "failed to prepare scan output", "", -1, "", ""});
#endif
    std::string rendered_command = render_process_for_log(process);

    if (listener_)
        listener_->on_nmap_command_ready(rendered_command);

    logger_.info(std::string(LOG_RUNNING_COMMAND_PREFIX) + rendered_command);
    return execute_scan_command({process, rendered_command, listener_}, logger_);
}
