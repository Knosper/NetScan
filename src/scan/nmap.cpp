#include "scan/nmap.hpp"

#ifdef _WIN32
#include "util/windows_executable_path.hpp"

#include <string>

// Uses the same lookup as the scan runner; where.exe would also accept an nmap.exe planted in
// the current directory.
NmapCheckResult check_nmap()
{
    std::wstring resolved;
    std::string error;
    if (!util::resolve_windows_executable("nmap", util::current_windows_search_path(), resolved,
                                          error))
        return {HealthCheckStatus::Missing, ""};

    return {HealthCheckStatus::Ok, ""};
}

#else
#include <unistd.h>
#include <sys/stat.h>
#include <cstdlib>
#include <sstream>
#include <string>

static bool is_regular_executable(const std::string& path)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0)
        return false;
    return S_ISREG(st.st_mode) && (access(path.c_str(), X_OK) == 0);
}

NmapCheckResult check_nmap()
{
    const char* path_env = std::getenv("PATH");
    if (path_env == nullptr)
        return {HealthCheckStatus::Error, "PATH environment variable not set"};

    std::istringstream ss(path_env);
    std::string dir;
    while (std::getline(ss, dir, ':'))
    {
        if (dir.empty())
            continue;
        if (is_regular_executable(dir + "/nmap"))
            return {HealthCheckStatus::Ok, ""};
    }

    return {HealthCheckStatus::Missing, ""};
}

#endif
