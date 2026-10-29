#ifndef APP_CONFIG_HPP
#define APP_CONFIG_HPP

#include "util/logger.hpp"
#include <string>
#include <vector>

// CLI overrides that take precedence over the config file.
struct CliOverrides
{
    bool ui_set = false;
    bool ui_value = false;
    bool web_dir_set = false;
    std::string web_dir;
};

// Full persisted configuration as stored on disk.
// This is the startup input model and may include restart-required values.
struct PersistedConfig
{
    std::string host;
    int port;
    std::string db_path;
    std::string web_dir;
    std::string log_level;
    std::string log_file;
    bool ui_enabled;
    int scan_cooldown_seconds = 0;
    bool tls_enabled = false;
    std::string tls_cert_path;
    std::string tls_key_path;
    // When true, bootstrap skips normal startup and runs the setup server
    // on (host, port). Cleared by the setup server once /api/setup completes.
    bool setup_pending = false;
    // Absolute path to the nmap executable. Empty means PATH lookup (default).
    std::string nmap_path;
    // Comma-separated extra Host header names accepted on a loopback bind (e.g. the
    // public name of a reverse proxy in front of NetScan). Empty by default.
    std::string allowed_hosts;
};

// Live-editable application settings.
// Only values that are safe to edit without restarting the process belong here.
struct AppSettings
{
    std::string log_level;
    int scan_cooldown_seconds = 0;
};

// Fully resolved runtime configuration for the started process.
struct AppConfig
{
    std::string host;
    int port;
    std::string db_path;
    std::string web_dir;
    std::string log_level;
    std::string log_file;
    bool ui_enabled;
    bool tls_enabled = false;
    std::string tls_cert_path;
    std::string tls_key_path;
    // Absolute path to the nmap executable. Empty means PATH lookup (default).
    std::string nmap_path;
    // Lower-cased entries of PersistedConfig::allowed_hosts.
    std::vector<std::string> allowed_hosts;
};

PersistedConfig make_default_persisted_config();
AppConfig make_default_config();
AppSettings make_default_settings();
PersistedConfig load_persisted_config(const std::string& path);
const char* default_tls_cert_path();
const char* default_tls_key_path();
// Same as load_persisted_config, but treats any parse/validation error as
// untrusted input: logs the error, rewrites the file with defaults +
// setup_pending=true, and returns those defaults.
PersistedConfig load_persisted_config_safe(const std::string& path, Logger& logger);
AppSettings load_raw_settings(const std::string& path);
AppSettings project_settings(const PersistedConfig& persisted);
AppConfig resolve_runtime_config(const PersistedConfig& persisted, const std::string& path);
AppConfig load_config(const std::string& path, Logger& logger);
bool validate_settings(const AppSettings& settings, std::string& error);
bool validate_persisted_config_for_startup(const PersistedConfig& persisted, std::string& error);
// IPv4/IPv6 literal or RFC 1123 hostname; rejects anything that could break conf.ini.
bool is_valid_bind_host(const std::string& host);
// Maps "localhost" to 127.0.0.1 so bind decisions never depend on name resolution.
std::string normalize_bind_host(const std::string& host);
// Bind-side security decisions (open mode, key/TLS requirement, Host allowlist):
// IP literals only (127.0.0.0/8, ::1). Hostnames are resolved by the HTTP library
// at bind time and could point anywhere, so "localhost" is not loopback here.
bool is_loopback_host(const std::string& host);
// Request-side Host header check: "localhost" or a loopback IP literal.
bool is_loopback_request_host(const std::string& hostname);
// Splits the comma-separated allowed_hosts value into trimmed, lower-cased names.
std::vector<std::string> parse_allowed_hosts(const std::string& value);
// Non-loopback binds need an admin key and TLS. Shared by setup and startup.
bool validate_bind_security(const std::string& host, bool has_admin_key, bool tls_enabled,
                            std::string& error);
std::string serialize_persisted_config(const PersistedConfig& persisted);
bool write_persisted_config_atomically(const std::string& path, const PersistedConfig& persisted,
                                       std::string& error);
bool write_settings_file_atomically(const std::string& path, const AppSettings& settings,
                                    std::string& error);
AppConfig apply_cli_overrides(const AppConfig& config, const CliOverrides& overrides);

#endif
