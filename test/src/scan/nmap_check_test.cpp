#include "scan/nmap.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "test_output.hpp"

namespace
{
bool expect(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << "\n";
        return false;
    }
    return true;
}

#ifdef _WIN32
std::wstring make_temp_dir(const wchar_t* suffix)
{
    wchar_t temp_dir[MAX_PATH];
    const DWORD length = GetTempPathW(MAX_PATH, temp_dir);
    if (length == 0 || length >= MAX_PATH)
        return std::wstring();
    const std::wstring path = std::wstring(temp_dir) + L"netscan-nmap-check-test-" +
                              std::to_wstring(static_cast<unsigned long>(GetCurrentProcessId())) +
                              suffix;
    if (CreateDirectoryW(path.c_str(), nullptr) == 0 && GetLastError() != ERROR_ALREADY_EXISTS)
        return std::wstring();
    return path;
}

bool create_empty_file(const std::wstring& path)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    CloseHandle(file);
    return true;
}

bool run_windows_checks()
{
    bool all_ok = true;
    const std::wstring cwd = make_temp_dir(L"-cwd");
    const std::wstring bin_dir = make_temp_dir(L"-bin");
    const std::wstring cwd_nmap = cwd + L"\\nmap.exe";
    const std::wstring bin_nmap = bin_dir + L"\\nmap.exe";
    wchar_t previous_dir[MAX_PATH];
    const DWORD previous_dir_length = GetCurrentDirectoryW(MAX_PATH, previous_dir);
    wchar_t previous_path[32767];
    const DWORD previous_path_length = GetEnvironmentVariableW(L"PATH", previous_path, 32767);
    if (!expect(!cwd.empty() && !bin_dir.empty() && create_empty_file(cwd_nmap) &&
                    create_empty_file(bin_nmap) && previous_dir_length > 0 &&
                    previous_dir_length < MAX_PATH && previous_path_length < 32767 &&
                    SetCurrentDirectoryW(cwd.c_str()) != 0,
                "windows nmap check fixtures should be created"))
        return false;

    SetEnvironmentVariableW(L"PATH", (L".;;" + bin_dir).c_str());
    all_ok = expect(check_nmap().status == HealthCheckStatus::Ok,
                    "check_nmap should find nmap.exe in an absolute PATH entry") &&
             all_ok;

    DeleteFileW(bin_nmap.c_str());
    all_ok = expect(check_nmap().status == HealthCheckStatus::Missing,
                    "check_nmap must not accept nmap.exe from the current directory") &&
             all_ok;

    SetCurrentDirectoryW(previous_dir);
    SetEnvironmentVariableW(L"PATH", previous_path_length > 0 ? previous_path : nullptr);
    DeleteFileW(cwd_nmap.c_str());
    RemoveDirectoryW(cwd.c_str());
    RemoveDirectoryW(bin_dir.c_str());
    return all_ok;
}
#else

std::string make_temp_dir()
{
    std::string path = "/tmp/netscan-nmap-check-test-XXXXXX";
    std::vector<char> buffer(path.begin(), path.end());
    buffer.push_back('\0');
    char* dir = mkdtemp(buffer.data());
    return dir == nullptr ? std::string() : std::string(dir);
}

bool write_executable_file(const std::string& path)
{
    std::ofstream out(path.c_str(), std::ios::out | std::ios::trunc | std::ios::binary);
    if (!out.is_open())
        return false;
    out << "#!/bin/sh\nexit 0\n";
    out.close();
    return chmod(path.c_str(), 0700) == 0;
}

class ScopedPathEnv
{
public:
    ScopedPathEnv() : had_path_(std::getenv("PATH") != nullptr),
                      original_(had_path_ ? std::getenv("PATH") : "")
    {
    }

    ~ScopedPathEnv()
    {
        if (had_path_)
            setenv("PATH", original_.c_str(), 1);
        else
            unsetenv("PATH");
    }

private:
    bool        had_path_;
    std::string original_;
};
#endif
} // namespace

int main()
{
    bool all_ok = true;

#ifdef _WIN32
    all_ok = run_windows_checks() && all_ok;
#else
    ScopedPathEnv path_env_guard;

    const std::string cwd = make_temp_dir();
    const std::string bin_dir = make_temp_dir();
    all_ok = expect(!cwd.empty(), "cwd fixture directory should be created") && all_ok;
    all_ok = expect(!bin_dir.empty(), "bin fixture directory should be created") && all_ok;

    if (!cwd.empty() && !bin_dir.empty())
    {
        const std::string cwd_nmap = cwd + "/nmap";
        const std::string bin_nmap = bin_dir + "/nmap";
        all_ok = expect(write_executable_file(cwd_nmap), "cwd nmap fixture should be created") &&
                 all_ok;
        all_ok = expect(write_executable_file(bin_nmap), "bin nmap fixture should be created") &&
                 all_ok;

        char previous_dir[PATH_MAX + 1] = {0};
        const char* previous_dir_result = getcwd(previous_dir, PATH_MAX);
        all_ok = expect(previous_dir_result != nullptr, "getcwd should return current directory") &&
                 all_ok;

        if (previous_dir_result != nullptr)
        {
            all_ok = expect(chdir(cwd.c_str()) == 0, "test should switch to cwd fixture") && all_ok;

            setenv("PATH", (":" + bin_dir).c_str(), 1);
            NmapCheckResult result = check_nmap();
            all_ok = expect(result.status == HealthCheckStatus::Ok,
                            "check_nmap should find nmap in non-empty PATH component") &&
                     all_ok;

            std::remove(bin_nmap.c_str());
            result = check_nmap();
            all_ok = expect(result.status == HealthCheckStatus::Missing,
                            "check_nmap should ignore empty PATH components and not use cwd") &&
                     all_ok;

            all_ok = expect(chdir(previous_dir) == 0, "test should restore original cwd") && all_ok;
        }

        std::remove(cwd_nmap.c_str());
        std::remove(bin_nmap.c_str());
        rmdir(cwd.c_str());
        rmdir(bin_dir.c_str());
    }
#endif

    return finish_test("nmap_check_test", all_ok);
}
