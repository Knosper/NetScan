#ifndef APP_SETUP_BOOTSTRAP_HPP
#define APP_SETUP_BOOTSTRAP_HPP

#include "app/config.hpp"
#include "util/logger.hpp"
#include <nlohmann/json.hpp>
#include <string>

enum class SetupTlsMode
{
    Disabled,
    SelfSigned,
    ExistingFiles
};

struct SetupPlan
{
    std::string host;
    int         port = 8080;
    SetupTlsMode tls_mode = SetupTlsMode::Disabled;
    std::string tls_cert_path;
    std::string tls_key_path;
    bool        generate_admin_key = false;
    bool        generate_user_key = false;
};

struct SetupResult
{
    bool ok = false;
    std::string admin_api_key;
    std::string user_api_key;
    std::string error;
};

bool parse_setup_host_port(const nlohmann::json& body, std::string& host, int& port,
                           std::string& error);
const char* admin_secret_name();
const char* restricted_secret_name();
const char* secret_name_for_cli_key(const std::string& key_name);
bool is_supported_key_name(const std::string& key_name);
PersistedConfig make_setup_persisted_config();
bool is_readable_regular_file(const std::string& path, std::string& error);
bool build_and_write_self_signed_cert(const std::string& ip, const std::string& cert_path,
                                      const std::string& key_path, int days, Logger& logger);
SetupResult apply_setup_plan(const SetupPlan& plan, const std::string& config_path,
                             Logger& logger);

#endif
