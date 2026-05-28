#include "app/paths.hpp"
#include "app/windows_launcher_config.hpp"
#include "app/windows_single_instance.hpp"

#include "util/path_utils.hpp"

#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>

#include <chrono>
#include <string>

namespace
{
const char* SERVER_EXE_NAME = "netscan-server.exe";

class WsaSession
{
public:
    WsaSession() : ok_(false)
    {
        WSADATA data;
        ok_ = (WSAStartup(MAKEWORD(2, 2), &data) == 0);
    }

    ~WsaSession()
    {
        if (ok_)
            WSACleanup();
    }

    bool ok() const { return ok_; }

private:
    bool ok_;
};

std::string join_windows_path(const std::string& left, const std::string& right)
{
    if (left.empty())
        return right;
    if (left.back() == '\\' || left.back() == '/')
        return left + right;
    return left + "\\" + right;
}

bool start_hidden_server(const std::string& server_path)
{
    STARTUPINFOA startup_info;
    PROCESS_INFORMATION process_info;
    ZeroMemory(&startup_info, sizeof(startup_info));
    ZeroMemory(&process_info, sizeof(process_info));
    startup_info.cb = sizeof(startup_info);
    startup_info.dwFlags = STARTF_USESHOWWINDOW;
    startup_info.wShowWindow = SW_HIDE;

    std::string command_line = "\"" + server_path + "\"";
    const BOOL created = CreateProcessA(server_path.c_str(),
                                        &command_line[0],
                                        nullptr,
                                        nullptr,
                                        FALSE,
                                        CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW,
                                        nullptr,
                                        nullptr,
                                        &startup_info,
                                        &process_info);
    if (!created)
        return false;

    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
    return true;
}

bool can_connect_once(const std::string& host, int port)
{
    addrinfo hints;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* result = nullptr;
    const std::string port_string = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_string.c_str(), &hints, &result) != 0)
        return false;

    bool connected = false;
    for (addrinfo* entry = result; entry != nullptr && !connected; entry = entry->ai_next)
    {
        SOCKET sock = socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
        if (sock == INVALID_SOCKET)
            continue;

        unsigned long non_blocking = 1;
        ioctlsocket(sock, FIONBIO, &non_blocking);
        const int connect_result = connect(sock, entry->ai_addr, static_cast<int>(entry->ai_addrlen));
        if (connect_result == 0)
        {
            connected = true;
        }
        else
        {
            fd_set write_set;
            FD_ZERO(&write_set);
            FD_SET(sock, &write_set);

            timeval timeout;
            timeout.tv_sec = 0;
            timeout.tv_usec = 300000;

            const int select_result = select(0, nullptr, &write_set, nullptr, &timeout);
            if (select_result > 0 && FD_ISSET(sock, &write_set))
            {
                int socket_error = 0;
                int opt_len = sizeof(socket_error);
                if (getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socket_error), &opt_len) == 0 &&
                    socket_error == 0)
                {
                    connected = true;
                }
            }
        }

        closesocket(sock);
    }

    freeaddrinfo(result);
    return connected;
}

bool wait_for_server_socket(const std::string& host, int port, std::chrono::milliseconds timeout)
{
    WsaSession wsa;
    if (!wsa.ok())
        return false;

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (can_connect_once(host, port))
            return true;
        Sleep(250);
    }

    return can_connect_once(host, port);
}

void open_url(const std::string& url)
{
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void show_error_message(const std::string& message)
{
    MessageBoxA(nullptr, message.c_str(), "NetScan", MB_OK | MB_ICONERROR);
}
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    const std::string executable_dir = get_executable_dir();
    const std::string server_path = join_windows_path(executable_dir, SERVER_EXE_NAME);
    const std::string config_path = get_default_config_path();
    const std::string config_root = dir_of(config_path);

    const WindowsLauncherConfig config = load_windows_launcher_config(config_path);
    if (is_windows_single_instance_active(config_root))
    {
        if (config.ui_enabled)
        {
            wait_for_server_socket(config.host, config.port, std::chrono::seconds(15));
            open_url(build_windows_launcher_url(config));
        }
        return 0;
    }

    if (!start_hidden_server(server_path))
    {
        if (config.ui_enabled && wait_for_server_socket(config.host, config.port, std::chrono::seconds(2)))
        {
            open_url(build_windows_launcher_url(config));
            return 0;
        }

        show_error_message("NetScan could not start the background server.");
        return 1;
    }

    if (config.ui_enabled)
    {
        if (!wait_for_server_socket(config.host, config.port, std::chrono::seconds(15)))
        {
            show_error_message("NetScan started, but the local web interface did not become ready in time.");
            return 1;
        }
        open_url(build_windows_launcher_url(config));
    }

    return 0;
}

#else

int main()
{
    return 1;
}

#endif
