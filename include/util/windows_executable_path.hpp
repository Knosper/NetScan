#ifndef UTIL_WINDOWS_EXECUTABLE_PATH_HPP
#define UTIL_WINDOWS_EXECUTABLE_PATH_HPP

#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>

#include "util/windows_argument_quote.hpp"

namespace util
{

inline bool is_windows_path_separator(wchar_t c)
{
    return c == L'\\' || c == L'/';
}

// Only drive-absolute ("C:\x") and UNC ("\\server\x") paths; "C:x" and "\x" depend on
// the current drive or directory.
inline bool is_absolute_windows_path(const std::wstring& path)
{
    if (path.size() >= 3 && path[1] == L':' && is_windows_path_separator(path[2]) &&
        ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')))
        return true;
    return path.size() >= 3 && is_windows_path_separator(path[0]) &&
           is_windows_path_separator(path[1]);
}

inline bool is_existing_windows_file(const std::wstring& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

inline bool full_windows_path(const std::wstring& path, std::wstring& full_path)
{
    const DWORD length = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (length == 0)
        return false;
    std::vector<wchar_t> buffer(static_cast<std::vector<wchar_t>::size_type>(length));
    const DWORD written = GetFullPathNameW(path.c_str(), length, &buffer[0], nullptr);
    if (written == 0 || written >= length)
        return false;
    full_path.assign(&buffer[0], written);
    return true;
}

inline std::wstring current_windows_search_path()
{
    const DWORD length = GetEnvironmentVariableW(L"PATH", nullptr, 0);
    if (length == 0)
        return std::wstring();
    std::vector<wchar_t> buffer(static_cast<std::vector<wchar_t>::size_type>(length));
    const DWORD written = GetEnvironmentVariableW(L"PATH", &buffer[0], length);
    if (written == 0 || written >= length)
        return std::wstring();
    return std::wstring(&buffer[0], written);
}

inline std::wstring windows_system_directory()
{
    wchar_t buffer[MAX_PATH];
    const UINT length = GetSystemDirectoryW(buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return std::wstring();
    return std::wstring(buffer, length);
}

// Resolves an executable to an absolute path without consulting the application directory or
// the current directory, which CreateProcess and where.exe search before PATH (binary planting).
// A bare name is searched only in the absolute entries of search_path (a PATH value) and gets
// ".exe" appended when it has no extension; a name with a directory part must be absolute.
inline bool resolve_windows_executable(const std::string& executable,
                                       const std::wstring& search_path, std::wstring& resolved,
                                       std::string& error)
{
    resolved.clear();
    std::wstring name = utf8_to_wide(executable);
    if (name.empty())
    {
        error = "invalid executable name: " + executable;
        return false;
    }

    if (name.find_first_of(L"\\/:") != std::wstring::npos)
    {
        if (!is_absolute_windows_path(name))
        {
            error = "executable path must be absolute: " + executable;
            return false;
        }
        if (!is_existing_windows_file(name) || !full_windows_path(name, resolved))
        {
            error = "executable not found: " + executable;
            return false;
        }
        return true;
    }

    if (name.find(L'.') == std::wstring::npos)
        name += L".exe";

    std::wstring::size_type start = 0;
    while (start <= search_path.size())
    {
        std::wstring::size_type end = search_path.find(L';', start);
        if (end == std::wstring::npos)
            end = search_path.size();
        std::wstring directory = search_path.substr(start, end - start);
        start = end + 1;

        if (directory.size() >= 2 && directory[0] == L'"' &&
            directory[directory.size() - 1] == L'"')
            directory = directory.substr(1, directory.size() - 2);
        // Relative PATH entries (including ".") resolve against the current directory.
        if (!is_absolute_windows_path(directory))
            continue;

        if (!is_windows_path_separator(directory[directory.size() - 1]))
            directory += L'\\';
        const std::wstring candidate = directory + name;
        if (is_existing_windows_file(candidate) && full_windows_path(candidate, resolved))
            return true;
    }

    error = "executable not found in PATH: " + executable;
    return false;
}

} // namespace util
#endif

#endif
