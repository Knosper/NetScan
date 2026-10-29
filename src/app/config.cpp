#include "app/config.hpp"
#include "app/paths.hpp"
#include "scan/target_validation.hpp"
#include "util/config_parse.hpp"
#include "util/file_permissions.hpp"
#include "util/path_utils.hpp"
#include "util/string_utils.hpp"

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

static const char* DEFAULT_HOST = "127.0.0.1";
static const int DEFAULT_PORT = 8080;
static const char* DEFAULT_DB_FILE = "./netscan.db";
#ifndef _WIN32
static const char* DEFAULT_WEB_DIR = "./resources/web";
#endif
static const char* DEFAULT_LOG_LEVEL = "info";
static const char* DEFAULT_TLS_CERT_PATH = "./certs/netscan.crt";
static const char* DEFAULT_TLS_KEY_PATH = "./certs/netscan.key";

namespace
{
int parse_port_value(const std::string& value, const std::string& context)
{
    try
    {
        int port = std::stoi(value);
        if (port < 1 || port > 65535)
            throw std::runtime_error(context + ": port out of range (1-65535)");
        return port;
    }
    catch (const std::invalid_argument&)
    {
        throw std::runtime_error(context + ": invalid port value");
    }
    catch (const std::out_of_range&)
    {
        throw std::runtime_error(context + ": port out of range (1-65535)");
    }
}

struct ConfigEntry
{
    std::string key;
    std::string value;
    int line_num = 0;
};

ConfigEntry parse_line(const std::string& line, int line_num)
{
    size_t eq = line.find('=');
    if (eq == std::string::npos)
        throw std::runtime_error("Config parse error on line " + std::to_string(line_num) +
                                 ": missing '='");

    ConfigEntry entry;
    entry.key = util::trim(line.substr(0, eq));
    entry.value = util::trim(line.substr(eq + 1));
    entry.line_num = line_num;

    if (entry.key.empty())
        throw std::runtime_error("Config parse error on line " + std::to_string(line_num) +
                                 ": empty key");
    return entry;
}

void apply_persisted_value(PersistedConfig& persisted, const ConfigEntry& entry)
{
    const std::string& k = entry.key;
    const std::string& v = entry.value;

    if (k == "host")
        persisted.host = normalize_bind_host(v);
    else if (k == "port")
        persisted.port = parse_port_value(
            v, "Config parse error on line " + std::to_string(entry.line_num));
    else if (k == "db_path")
        persisted.db_path = v;
    else if (k == "web_dir")
        persisted.web_dir = v;
    else if (k == "log_level")
        persisted.log_level = v;
    else if (k == "log_file")
        persisted.log_file = v;
    else if (k == "ui_enabled")
        persisted.ui_enabled = util::parse_config_bool(v);
    else if (k == "scan_cooldown_seconds")
    {
        try
        {
            int parsed = std::stoi(v);
            persisted.scan_cooldown_seconds = parsed >= 0 ? parsed : 0;
        }
        catch (...)
        {
            persisted.scan_cooldown_seconds = 0;
        }
    }
    else if (k == "tls_enabled")
        persisted.tls_enabled = util::parse_config_bool(v);
    else if (k == "tls_cert_path")
        persisted.tls_cert_path = v;
    else if (k == "tls_key_path")
        persisted.tls_key_path = v;
    else if (k == "setup_pending")
        persisted.setup_pending = util::parse_config_bool(v);
    else if (k == "nmap_path")
        persisted.nmap_path = v;
    else if (k == "allowed_hosts")
        persisted.allowed_hosts = v;
}

PersistedConfig load_persisted_config_from_stream(std::istream& stream)
{
    PersistedConfig persisted = make_default_persisted_config();
    std::string line;
    int line_num = 0;

    while (std::getline(stream, line))
    {
        ++line_num;
        line = util::trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;

        apply_persisted_value(persisted, parse_line(line, line_num));
    }

    return persisted;
}

bool write_text_file(const std::string& path, const std::string& content, std::string& error)
{
    // Write exact bytes so serialized config keeps canonical '\n' line endings on all platforms.
    std::ofstream file(path.c_str(), std::ios::out | std::ios::trunc | std::ios::binary);
    if (!file.is_open())
    {
        error = "failed to open temp settings file";
        return false;
    }

    file << content;
    if (!file.good())
    {
        error = "failed to write temp settings file";
        file.close();
        std::remove(path.c_str());
        return false;
    }

    file.close();
    if (!file)
    {
        error = "failed to flush temp settings file";
        std::remove(path.c_str());
        return false;
    }

    return true;
}

bool replace_file_atomically(const std::string& temp_path, const std::string& path,
                             std::string& error)
{
#ifdef _WIN32
    if (MoveFileExA(temp_path.c_str(), path.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0)
    {
        error = "failed to replace config file";
        std::remove(temp_path.c_str());
        return false;
    }
    return true;
#else
    if (std::rename(temp_path.c_str(), path.c_str()) != 0)
    {
        error = "failed to replace config file";
        std::remove(temp_path.c_str());
        return false;
    }
    return true;
#endif
}

// conf.ini is line-based: a line break inside a value would start a new key
// (e.g. nmap_path), so every serialized string must stay on its own line.
bool validate_config_values_single_line(const PersistedConfig& persisted, std::string& error)
{
    const struct
    {
        const char* name;
        const std::string* value;
    } fields[] = {
        {"host", &persisted.host},
        {"db_path", &persisted.db_path},
        {"web_dir", &persisted.web_dir},
        {"log_level", &persisted.log_level},
        {"log_file", &persisted.log_file},
        {"tls_cert_path", &persisted.tls_cert_path},
        {"tls_key_path", &persisted.tls_key_path},
        {"nmap_path", &persisted.nmap_path},
        {"allowed_hosts", &persisted.allowed_hosts},
    };
    for (const auto& field : fields)
    {
        if (util::contains_control_characters(*field.value))
        {
            error = std::string("field '") + field.name + "' must not contain control characters";
            return false;
        }
    }
    return true;
}

bool is_valid_hostname(const std::string& host)
{
    if (host.empty() || host.size() > 253)
        return false;

    std::string::size_type label_start = 0;
    while (label_start <= host.size())
    {
        std::string::size_type label_end = host.find('.', label_start);
        if (label_end == std::string::npos)
            label_end = host.size();
        const std::string::size_type label_len = label_end - label_start;
        if (label_len == 0 || label_len > 63)
            return false;
        if (host[label_start] == '-' || host[label_end - 1] == '-')
            return false;
        for (std::string::size_type i = label_start; i < label_end; ++i)
        {
            const unsigned char c = static_cast<unsigned char>(host[i]);
            if (!std::isalnum(c) && c != '-')
                return false;
        }
        label_start = label_end + 1;
    }
    return true;
}

// Restricts the config file to owner-read/write only. No-op on Windows;
// ACL hardening there belongs to the installer/AppData migration.
// chmod runs on the tmp file before the atomic rename so the final inode
// always has 0600 — POSIX rename preserves the source file's permissions
// when the target does not yet exist.
} // namespace

PersistedConfig make_default_persisted_config()
{
    PersistedConfig persisted;
    persisted.host = DEFAULT_HOST;
    persisted.port = DEFAULT_PORT;
    persisted.db_path = DEFAULT_DB_FILE;
#ifdef _WIN32
    persisted.web_dir = join_path(get_executable_dir(), "resources/web");
#else
    persisted.web_dir = DEFAULT_WEB_DIR;
#endif
    persisted.log_level = DEFAULT_LOG_LEVEL;
    persisted.log_file = "";
    persisted.ui_enabled = false;
    persisted.scan_cooldown_seconds = 0;
    persisted.tls_enabled = false;
    persisted.tls_cert_path = "";
    persisted.tls_key_path = "";
    persisted.setup_pending = false;
    persisted.nmap_path = "";
    persisted.allowed_hosts = "";
    return persisted;
}

const char* default_tls_cert_path()
{
    return DEFAULT_TLS_CERT_PATH;
}

const char* default_tls_key_path()
{
    return DEFAULT_TLS_KEY_PATH;
}

AppSettings make_default_settings()
{
    AppSettings settings;
    settings.log_level = DEFAULT_LOG_LEVEL;
    settings.scan_cooldown_seconds = 0;
    return settings;
}

AppSettings project_settings(const PersistedConfig& persisted)
{
    AppSettings settings;
    settings.log_level = persisted.log_level;
    settings.scan_cooldown_seconds = persisted.scan_cooldown_seconds;
    return settings;
}

AppConfig resolve_runtime_config(const PersistedConfig& persisted, const std::string& path)
{
    AppConfig config;
    const std::string config_dir = dir_of(to_absolute_path(path));

    config.host = persisted.host;
    config.port = persisted.port;
    config.db_path = resolve_path(config_dir, persisted.db_path);
    config.web_dir = resolve_path(config_dir, persisted.web_dir);
    config.log_level = persisted.log_level;
    if (!persisted.log_file.empty())
        config.log_file = resolve_path(config_dir, persisted.log_file);
    else
        config.log_file.clear();
    config.ui_enabled = persisted.ui_enabled;
    config.tls_enabled = persisted.tls_enabled;
    if (!persisted.tls_cert_path.empty())
        config.tls_cert_path = resolve_path(config_dir, persisted.tls_cert_path);
    else
        config.tls_cert_path.clear();
    if (!persisted.tls_key_path.empty())
        config.tls_key_path = resolve_path(config_dir, persisted.tls_key_path);
    else
        config.tls_key_path.clear();
    config.nmap_path = persisted.nmap_path;
    config.allowed_hosts = parse_allowed_hosts(persisted.allowed_hosts);

    return config;
}

AppConfig make_default_config()
{
    return resolve_runtime_config(make_default_persisted_config(), get_default_config_path());
}

PersistedConfig load_persisted_config(const std::string& path)
{
    std::ifstream file(path.c_str());
    if (!file.is_open())
        return make_default_persisted_config();

    return load_persisted_config_from_stream(file);
}

AppSettings load_raw_settings(const std::string& path)
{
    std::ifstream file(path.c_str());
    if (!file.is_open())
        return make_default_settings();

    return project_settings(load_persisted_config_from_stream(file));
}

// Loads config and treats any parse/validation error as untrusted input:
// resets to defaults with setup_pending=true so bootstrap drops into setup mode.
PersistedConfig load_persisted_config_safe(const std::string& path, Logger& logger)
{
    std::ifstream file(path.c_str());
    if (!file.is_open())
    {
        logger.warn("Config file not found: " + path + " - using defaults");
        return make_default_persisted_config();
    }

    PersistedConfig persisted;
    try
    {
        persisted = load_persisted_config_from_stream(file);
    }
    catch (const std::exception& e)
    {
        logger.error(std::string("Config parse error — resetting to defaults: ") + e.what());
        PersistedConfig reset_config = make_default_persisted_config();
        reset_config.setup_pending = true;
        std::string write_error;
        if (!write_persisted_config_atomically(path, reset_config, write_error))
            logger.error("Failed to persist reset config: " + write_error);
        return reset_config;
    }

    std::string validation_error;
    if (!validate_persisted_config_for_startup(persisted, validation_error))
    {
        logger.error("Config validation error — resetting to defaults: " + validation_error);
        PersistedConfig reset_config = make_default_persisted_config();
        reset_config.setup_pending = true;
        std::string write_error;
        if (!write_persisted_config_atomically(path, reset_config, write_error))
            logger.error("Failed to persist reset config: " + write_error);
        return reset_config;
    }

    return persisted;
}

AppConfig load_config(const std::string& path, Logger& logger)
{
    const PersistedConfig persisted = load_persisted_config_safe(path, logger);
    return resolve_runtime_config(persisted, path);
}

bool validate_settings(const AppSettings& settings, std::string& error)
{
    if (util::trim(settings.log_level).empty())
    {
        error = "field 'log_level' must not be empty";
        return false;
    }
    if (!is_supported_log_level(settings.log_level))
    {
        error = "field 'log_level' must be one of: debug, info, warn, error";
        return false;
    }
    if (settings.scan_cooldown_seconds < 0)
    {
        error = "field 'scan_cooldown_seconds' must be >= 0";
        return false;
    }
    return true;
}

bool validate_persisted_config_for_startup(const PersistedConfig& persisted, std::string& error)
{
    if (!validate_config_values_single_line(persisted, error))
        return false;
    if (util::trim(persisted.host).empty())
    {
        error = "field 'host' must not be empty";
        return false;
    }
    if (!is_valid_bind_host(persisted.host))
    {
        error = "field 'host' must be an IP address or hostname";
        return false;
    }
    if (persisted.port < 1 || persisted.port > 65535)
    {
        error = "field 'port' must be between 1 and 65535";
        return false;
    }
    if (util::trim(persisted.db_path).empty())
    {
        error = "field 'db_path' must not be empty";
        return false;
    }
    if (util::trim(persisted.web_dir).empty())
    {
        error = "field 'web_dir' must not be empty";
        return false;
    }
    if (util::trim(persisted.log_level).empty())
    {
        error = "field 'log_level' must not be empty";
        return false;
    }
    if (!is_supported_log_level(persisted.log_level))
    {
        error = "field 'log_level' must be one of: debug, info, warn, error";
        return false;
    }
    const std::vector<std::string> allowed_hosts = parse_allowed_hosts(persisted.allowed_hosts);
    for (const std::string& name : allowed_hosts)
    {
        if (!is_valid_bind_host(name))
        {
            error = "field 'allowed_hosts' must be a comma-separated list of hostnames, got '" +
                    name + "'";
            return false;
        }
    }
    return true;
}

std::string serialize_persisted_config(const PersistedConfig& persisted)
{
    std::ostringstream out;
    out << "host=" << persisted.host << "\n";
    out << "port=" << persisted.port << "\n";
    out << "db_path=" << persisted.db_path << "\n";
    out << "web_dir=" << persisted.web_dir << "\n";
    out << "log_level=" << persisted.log_level << "\n";
    if (persisted.log_file.empty())
        out << "# log_file=./netscan.log\n";
    else
        out << "log_file=" << persisted.log_file << "\n";
    out << "ui_enabled=" << (persisted.ui_enabled ? "true" : "false") << "\n";
    out << "scan_cooldown_seconds=" << persisted.scan_cooldown_seconds << "\n";
    out << "tls_enabled=" << (persisted.tls_enabled ? "true" : "false") << "\n";
    if (!persisted.tls_cert_path.empty())
        out << "tls_cert_path=" << persisted.tls_cert_path << "\n";
    else
        out << "# tls_cert_path=" << default_tls_cert_path() << "\n";
    if (!persisted.tls_key_path.empty())
        out << "tls_key_path=" << persisted.tls_key_path << "\n";
    else
        out << "# tls_key_path=" << default_tls_key_path() << "\n";
    if (persisted.setup_pending)
        out << "setup_pending=true\n";
    if (!persisted.nmap_path.empty())
        out << "nmap_path=" << persisted.nmap_path << "\n";
    if (!persisted.allowed_hosts.empty())
        out << "allowed_hosts=" << persisted.allowed_hosts << "\n";
    return out.str();
}

bool is_valid_bind_host(const std::string& host)
{
    return util::is_numeric_ip_literal(host) || is_valid_hostname(host);
}

std::string normalize_bind_host(const std::string& host)
{
    return util::to_lower_ascii(host) == "localhost" ? std::string(DEFAULT_HOST) : host;
}

bool is_loopback_host(const std::string& host)
{
    std::uint32_t address = 0;
    if (parse_ipv4_literal(host, address))
        return (address & 0xFF000000u) == 0x7F000000u;

    std::uint16_t groups[8] = {};
    if (host.find(':') == std::string::npos || !parse_ipv6_literal(host, groups))
        return false;
    for (int i = 0; i < 7; ++i)
    {
        if (groups[i] != 0)
            return false;
    }
    return groups[7] == 1;
}

bool is_loopback_request_host(const std::string& hostname)
{
    return util::to_lower_ascii(hostname) == "localhost" || is_loopback_host(hostname);
}

std::vector<std::string> parse_allowed_hosts(const std::string& value)
{
    std::vector<std::string> names;
    std::string::size_type start = 0;
    while (start <= value.size())
    {
        std::string::size_type end = value.find(',', start);
        if (end == std::string::npos)
            end = value.size();
        const std::string name = util::to_lower_ascii(util::trim(value.substr(start, end - start)));
        if (!name.empty())
            names.push_back(name);
        start = end + 1;
    }
    return names;
}

bool validate_bind_security(const std::string& host, bool has_admin_key, bool tls_enabled,
                            std::string& error)
{
    if (is_loopback_host(host))
        return true;
    if (!has_admin_key)
    {
        error = "non-loopback binding requires an admin API key; generate one during setup "
                "or with './netscan --generate-key admin'";
        return false;
    }
    if (!tls_enabled)
    {
        error = "non-loopback binding requires TLS; set tls_enabled=true and configure "
                "tls_cert_path/tls_key_path (see docs/tls-setup.md)";
        return false;
    }
    return true;
}

bool write_persisted_config_atomically(const std::string& path, const PersistedConfig& persisted,
                                      std::string& error)
{
    if (!validate_persisted_config_for_startup(persisted, error))
        return false;

    const std::string temp_path = path + ".tmp";
    if (!write_text_file(temp_path, serialize_persisted_config(persisted), error))
        return false;

    if (!util::restrict_file_to_owner(temp_path, &error))
    {
        std::remove(temp_path.c_str());
        return false;
    }
    return replace_file_atomically(temp_path, path, error);
}

bool write_settings_file_atomically(const std::string& path, const AppSettings& settings,
                                    std::string& error)
{
    if (!validate_settings(settings, error))
        return false;

    PersistedConfig persisted = load_persisted_config(path);
    persisted.log_level = settings.log_level;
    persisted.scan_cooldown_seconds = settings.scan_cooldown_seconds;
    return write_persisted_config_atomically(path, persisted, error);
}

AppConfig apply_cli_overrides(const AppConfig& config, const CliOverrides& overrides)
{
    AppConfig updated = config;
    if (overrides.ui_set)
        updated.ui_enabled = overrides.ui_value;
    if (overrides.web_dir_set)
        updated.web_dir = overrides.web_dir;
    return updated;
}
