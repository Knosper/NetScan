#include "platform.hpp"
#include "app/paths.hpp"

#include "util/path_utils.hpp"

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <limits.h>
#include <stdlib.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

std::string get_executable_dir()
{
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, buf, MAX_PATH);
    if (len == 0 || len == MAX_PATH)
        return ".";
    std::string path(buf, len);
#elif defined(__APPLE__)
    char buf[PATH_MAX];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0)
        return ".";
    char resolved[PATH_MAX];
    if (realpath(buf, resolved) == nullptr)
        return ".";
    std::string path(resolved);
#else
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0)
        return ".";
    buf[len] = '\0';
    std::string path(buf);
#endif
    std::string::size_type pos = path.find_last_of("/\\");
    if (pos == std::string::npos)
        return ".";
    return path.substr(0, pos);
}

std::string get_default_config_path()
{
#ifdef _WIN32
    char local_app_data[MAX_PATH];
    DWORD len = GetEnvironmentVariableA("LOCALAPPDATA", local_app_data, MAX_PATH);
    if (len > 0 && len < MAX_PATH)
        return join_path(join_path(std::string(local_app_data, len), "NetScan"), "conf.ini");
#endif
    return join_path(get_executable_dir(), "conf.ini");
}
