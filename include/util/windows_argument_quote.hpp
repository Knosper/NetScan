#ifndef UTIL_WINDOWS_ARGUMENT_QUOTE_HPP
#define UTIL_WINDOWS_ARGUMENT_QUOTE_HPP

#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>

namespace util
{

inline std::wstring utf8_to_wide(const std::string& input)
{
    if (input.empty())
        return std::wstring();

    const int wide_length = MultiByteToWideChar(CP_UTF8, 0, input.c_str(), -1, nullptr, 0);
    if (wide_length <= 0)
        return std::wstring();

    std::vector<wchar_t> buffer(static_cast<std::vector<wchar_t>::size_type>(wide_length));
    if (MultiByteToWideChar(CP_UTF8, 0, input.c_str(), -1, &buffer[0], wide_length) <= 0)
        return std::wstring();

    return std::wstring(&buffer[0]);
}

inline bool needs_windows_quotes(const std::string& argument)
{
    if (argument.empty())
        return true;

    for (std::string::size_type i = 0; i < argument.size(); ++i)
    {
        const char c = argument[i];
        if (c == ' ' || c == '\t' || c == '"')
            return true;
    }

    return false;
}

inline std::wstring quote_windows_argument(const std::string& argument)
{
    const std::wstring wide_argument = utf8_to_wide(argument);
    if (!needs_windows_quotes(argument))
        return wide_argument;

    std::wstring quoted;
    quoted += L'"';

    std::wstring::size_type backslash_count = 0;
    for (std::wstring::size_type i = 0; i < wide_argument.size(); ++i)
    {
        const wchar_t c = wide_argument[i];
        if (c == L'\\')
        {
            ++backslash_count;
            continue;
        }

        if (c == L'"')
        {
            quoted.append(backslash_count * 2 + 1, L'\\');
            quoted += L'"';
            backslash_count = 0;
            continue;
        }

        if (backslash_count > 0)
        {
            quoted.append(backslash_count, L'\\');
            backslash_count = 0;
        }

        quoted += c;
    }

    if (backslash_count > 0)
        quoted.append(backslash_count * 2, L'\\');

    quoted += L'"';
    return quoted;
}

inline std::wstring build_windows_command_line(const std::vector<std::string>& args)
{
    std::wstring cmdline;
    for (auto it = args.begin(); it != args.end(); ++it)
    {
        if (!cmdline.empty())
            cmdline += L' ';
        cmdline += quote_windows_argument(*it);
    }
    return cmdline;
}

} // namespace util
#endif

#endif
