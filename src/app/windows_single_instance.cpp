#include "app/windows_single_instance.hpp"

#ifdef _WIN32

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <sstream>

namespace
{
std::string normalize_install_root(const std::string& install_root)
{
    std::string normalized = install_root;
    std::replace(normalized.begin(), normalized.end(), '/', '\\');
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return normalized;
}

std::uint64_t fnv1a64(const std::string& value)
{
    std::uint64_t hash = 1469598103934665603ull;
    for (unsigned char c : value)
    {
        hash ^= static_cast<std::uint64_t>(c);
        hash *= 1099511628211ull;
    }
    return hash;
}
}

WindowsSingleInstanceGuard::WindowsSingleInstanceGuard() : handle_(nullptr) {}

WindowsSingleInstanceGuard::~WindowsSingleInstanceGuard()
{
    if (handle_ != nullptr)
        CloseHandle(static_cast<HANDLE>(handle_));
}

bool WindowsSingleInstanceGuard::acquire_for_install_root(const std::string& install_root)
{
    if (handle_ != nullptr)
        return true;

    const std::string mutex_name = build_windows_single_instance_name(install_root);
    HANDLE handle = CreateMutexA(nullptr, FALSE, mutex_name.c_str());
    if (handle == nullptr)
        return false;

    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(handle);
        return false;
    }

    handle_ = handle;
    return true;
}

bool WindowsSingleInstanceGuard::acquired() const
{
    return handle_ != nullptr;
}

std::string build_windows_single_instance_name(const std::string& install_root)
{
    const std::string normalized = normalize_install_root(install_root);
    const std::uint64_t hash = fnv1a64(normalized);

    std::ostringstream out;
    out << "Local\\NetScanServerInstance-" << std::hex << hash;
    return out.str();
}

bool is_windows_single_instance_active(const std::string& install_root)
{
    const std::string mutex_name = build_windows_single_instance_name(install_root);
    HANDLE handle = OpenMutexA(SYNCHRONIZE, FALSE, mutex_name.c_str());
    if (handle == nullptr)
        return false;

    CloseHandle(handle);
    return true;
}

#else

WindowsSingleInstanceGuard::WindowsSingleInstanceGuard() : handle_(nullptr) {}
WindowsSingleInstanceGuard::~WindowsSingleInstanceGuard() {}

bool WindowsSingleInstanceGuard::acquire_for_install_root(const std::string&)
{
    handle_ = reinterpret_cast<void*>(1);
    return true;
}

bool WindowsSingleInstanceGuard::acquired() const
{
    return handle_ != nullptr;
}

std::string build_windows_single_instance_name(const std::string& install_root)
{
    return install_root;
}

bool is_windows_single_instance_active(const std::string&)
{
    return false;
}

#endif
