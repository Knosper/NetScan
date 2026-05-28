#include "app/windows_launcher_config.hpp"

#include "util/config_parse.hpp"

#include <fstream>
#include <sstream>

namespace
{
std::string trim_copy(const std::string& value)
{
    const std::string whitespace = " \t\r\n";
    const std::string::size_type start = value.find_first_not_of(whitespace);
    if (start == std::string::npos)
        return "";

    const std::string::size_type end = value.find_last_not_of(whitespace);
    return value.substr(start, end - start + 1);
}
}

WindowsLauncherConfig load_windows_launcher_config(const std::string& config_path)
{
    WindowsLauncherConfig config;

    std::ifstream in(config_path.c_str());
    if (!in)
        return config;

    std::string line;
    while (std::getline(in, line))
    {
        line = trim_copy(line);
        if (line.empty() || line[0] == '#')
            continue;

        const std::string::size_type pos = line.find('=');
        if (pos == std::string::npos)
            continue;

        const std::string key = trim_copy(line.substr(0, pos));
        const std::string value = trim_copy(line.substr(pos + 1));

        if (key == "host" && !value.empty())
            config.host = value;
        else if (key == "port")
        {
            std::istringstream port_stream(value);
            int parsed_port = 0;
            if ((port_stream >> parsed_port) && parsed_port >= 1 && parsed_port <= 65535)
                config.port = parsed_port;
        }
        else if (key == "tls_enabled")
            config.tls_enabled = util::parse_config_bool(value);
        else if (key == "ui_enabled")
            config.ui_enabled = util::parse_config_bool(value);
    }

    return config;
}

std::string build_windows_launcher_url(const WindowsLauncherConfig& config)
{
    const std::string scheme = config.tls_enabled ? "https" : "http";
    return scheme + "://" + config.host + ":" + std::to_string(config.port);
}
