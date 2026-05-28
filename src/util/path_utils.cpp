#include "util/path_utils.hpp"

#include <cerrno>
#include <cctype>
#include <cstring>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
#define MKDIR(p) _mkdir(p)
#define ACCESS _access
#define R_OK_FLAG 4
#define W_OK_FLAG 2
#else
#include <unistd.h>
#include <limits.h>
#define MKDIR(p) mkdir((p), 0700)
#define ACCESS access
#define R_OK_FLAG R_OK
#define W_OK_FLAG W_OK
#endif

std::string join_path(const std::string& a, const std::string& b)
{
    if (a.empty())
        return b;
    if (b.empty())
        return a;

    if (a.back() == '/' || a.back() == '\\')
        return a + b;

    return a + '/' + b;
}

bool is_absolute_path(const std::string& p)
{
    if (p.empty())
        return false;
#ifdef _WIN32
    // e.g. C:\... or C:/...
    if (p.size() >= 3 && std::isalpha((unsigned char)p[0]) && p[1] == ':' && (p[2] == '\\' || p[2] == '/'))
        return true;
    // UNC paths \\server\share
    if (p.size() >= 2 && p[0] == '\\' && p[1] == '\\')
        return true;
    return false;
#else
    return p[0] == '/';
#endif
}

std::string dir_of(const std::string& file_path)
{
    std::string::size_type pos = file_path.find_last_of("/\\");
    if (pos == std::string::npos)
        return ".";
    return file_path.substr(0, pos);
}

std::string resolve_path(const std::string& base_dir, const std::string& p)
{
    if (is_absolute_path(p))
        return p;
    return join_path(base_dir, p);
}

std::string to_absolute_path(const std::string& p)
{
    if (is_absolute_path(p))
        return p;
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD result = GetFullPathNameA(p.c_str(), MAX_PATH, buf, nullptr);
    if (result == 0 || result >= MAX_PATH)
        return p;
    return buf;
#else
    char buf[PATH_MAX];
    if (getcwd(buf, sizeof(buf)) == nullptr)
        return p;
    return join_path(buf, p);
#endif
}

bool path_exists(const std::string& path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

bool is_regular_file(const std::string& path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool is_directory(const std::string& path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool is_readable(const std::string& path)
{
    return ACCESS(path.c_str(), R_OK_FLAG) == 0;
}

bool is_writable(const std::string& path)
{
    return ACCESS(path.c_str(), W_OK_FLAG) == 0;
}

bool ensure_directory_exists(const std::string& path, std::string* error)
{
    if (path_exists(path))
    {
        if (is_directory(path))
            return true;
        if (error)
            *error = "Path exists but is not a directory: " + path;
        return false;
    }

    if (MKDIR(path.c_str()) == 0)
        return true;

    if (error)
        *error = std::string("Failed to create directory: ") + path + " (" +
                 std::strerror(errno) + ")";
    return false;
}
