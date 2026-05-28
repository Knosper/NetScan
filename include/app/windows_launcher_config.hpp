#ifndef APP_WINDOWS_LAUNCHER_CONFIG_HPP
#define APP_WINDOWS_LAUNCHER_CONFIG_HPP

#include <string>

struct WindowsLauncherConfig
{
    std::string host = "127.0.0.1";
    int port = 8080;
    bool tls_enabled = false;
    bool ui_enabled = true;
};

WindowsLauncherConfig load_windows_launcher_config(const std::string& config_path);
std::string build_windows_launcher_url(const WindowsLauncherConfig& config);

#endif
