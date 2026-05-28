#include "scan/nmap.hpp"

#ifdef _WIN32
#include <cstdio>

static const int NMAP_CHECK_BUFFER_SIZE = 256;

NmapCheckResult check_nmap()
{
    FILE* pipe = _popen("where nmap 2>nul", "r");

    if (pipe == nullptr)
        return {HealthCheckStatus::Error, "failed to open pipe for availability check"};

    char buf[NMAP_CHECK_BUFFER_SIZE];
    bool found = (std::fgets(buf, sizeof(buf), pipe) != nullptr);
    int rc = _pclose(pipe);

    if (rc == -1)
        return {HealthCheckStatus::Error, "_pclose failed"};

    // where.exe exits 0 when found, 1 when not found
    if (!found || rc != 0)
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
