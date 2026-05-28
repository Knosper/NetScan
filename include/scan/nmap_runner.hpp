#ifndef NMAP_RUNNER_HPP
#define NMAP_RUNNER_HPP

#include "platform.hpp"
#include "scan/process_spec.hpp"

#ifndef _WIN32
#include <memory>
#include <mutex>
#endif
#include <string>
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <sys/types.h>
#endif

#ifndef _WIN32
struct NmapProcessLifecycle
{
    std::mutex mutex;
    bool finished = false;
};
#endif

struct NmapProcessHandle
{
#ifdef _WIN32
    HANDLE process = nullptr;
    HANDLE job = nullptr;
#else
    pid_t pid = -1;
    pid_t process_group_id = -1;
    std::shared_ptr<NmapProcessLifecycle> lifecycle;
#endif
};

class NmapProcessListener
{
public:
    virtual ~NmapProcessListener() {}
    virtual void on_nmap_process_started(const NmapProcessHandle& handle) = 0;
    virtual void on_nmap_process_finished() = 0;
    virtual void on_nmap_progress(int percent) { (void)percent; }
    virtual void on_nmap_command_ready(const std::string& cmd) { (void)cmd; }
    virtual void on_nmap_eta(int seconds_remaining) { (void)seconds_remaining; }
    virtual void on_nmap_hosts_found(int count) { (void)count; }
};

#include "util/logger.hpp"

struct NmapRunResult
{
    bool ok = false;
    int exit_code = -1;
    std::string output;
    std::string stderr_text;
    std::string error;
};

NmapRunResult run_nmap_process(const ProcessSpec& process, Logger* logger = nullptr,
                               NmapProcessListener* listener = nullptr);
bool terminate_nmap_process(const NmapProcessHandle& handle, std::string* error = nullptr);
bool force_terminate_nmap_process(const NmapProcessHandle& handle,
                                  std::string* error = nullptr);

#endif
