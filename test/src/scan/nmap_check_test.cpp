#include "scan/nmap.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "test_output.hpp"

namespace
{
#ifndef _WIN32
bool expect(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << "\n";
        return false;
    }
    return true;
}

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

#ifndef _WIN32
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
