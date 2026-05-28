#include "app/application_bootstrap.hpp"
#include "app/setup_bootstrap.hpp"

#include "app/paths.hpp"
#include "scan/target_validation.hpp"
#include "util/path_utils.hpp"
#include "util/string_utils.hpp"
#include <cctype>
#include <cstdio>
#include <iostream>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace
{
bool prompt_line(const std::string& prompt, std::string& value)
{
    std::cout << prompt;
    std::cout.flush();
    return static_cast<bool>(std::getline(std::cin, value));
}

bool prompt_yes_no(const std::string& prompt, bool default_value, bool& answer)
{
    while (true)
    {
        std::string line;
        if (!prompt_line(prompt, line))
            return false;

        line = util::trim(line);
        if (line.empty())
        {
            answer = default_value;
            return true;
        }

        for (std::string::size_type i = 0; i < line.size(); ++i)
            line[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(line[i])));

        if (line == "y" || line == "yes")
        {
            answer = true;
            return true;
        }

        if (line == "n" || line == "no")
        {
            answer = false;
            return true;
        }

        std::cout << "Please answer y or n.\n";
    }
}
bool prompt_for_ipv4(const std::string& prompt, const std::string& default_value,
                     std::string& ip)
{
    std::string line;
    while (true)
    {
        if (!prompt_line(prompt, line))
            return false;

        line = util::trim(line);
        if (line.empty())
            line = default_value;

        if (is_valid_ipv4_literal(line))
        {
            ip = line;
            return true;
        }

        std::cout << "Please enter a valid IPv4 address.\n";
    }
}

bool prompt_for_existing_tls_paths(const std::string& config_dir, std::string& cert_path_out,
                                   std::string& key_path_out)
{
    while (true)
    {
        std::string cert_input;
        std::string key_input;
        if (!prompt_line("Certificate path: ", cert_input) ||
            !prompt_line("Private key path: ", key_input))
        {
            return false;
        }

        cert_input = util::trim(cert_input);
        key_input = util::trim(key_input);
        if (cert_input.empty() || key_input.empty())
        {
            std::cout << "Certificate path and key path are required.\n";
            continue;
        }

        const std::string cert_path =
            is_absolute_path(cert_input) ? cert_input : resolve_path(config_dir, cert_input);
        const std::string key_path =
            is_absolute_path(key_input) ? key_input : resolve_path(config_dir, key_input);

        std::string error;
        if (!is_readable_regular_file(cert_path, error))
        {
            std::cout << "Validation failed: " << error << "\n";
            continue;
        }
        if (!is_readable_regular_file(key_path, error))
        {
            std::cout << "Validation failed: " << error << "\n";
            continue;
        }

        cert_path_out = cert_path;
        key_path_out = key_path;
        return true;
    }
}
int check_tty_or_fail()
{
#ifdef _WIN32
    if (!_isatty(_fileno(stdin)))
#else
    if (!isatty(fileno(stdin)))
#endif
    {
        std::cerr << "Error: --setup requires an interactive terminal (stdin is not a TTY).\n"
                  << "Start NetScan without --setup to use the browser-based setup instead.\n";
        return 1;
    }
    return 0;
}

int confirm_overwrite_or_fail(const std::string& absolute_config_path, Logger& logger)
{
    if (!path_exists(absolute_config_path))
        return 0;

    bool overwrite = false;
    if (!prompt_yes_no("Config file already exists. Overwrite? [y/n] (default: no): ", false,
                       overwrite))
    {
        logger.error("Setup aborted while waiting for overwrite confirmation");
        return 1;
    }
    if (!overwrite)
    {
        std::cout << "Setup cancelled.\n";
        return 1;
    }
    return 0;
}

int prompt_host_into_plan(SetupPlan& plan, const std::string& absolute_config_path,
                          Logger& logger)
{
    std::cout << "Interactive setup for " << absolute_config_path << "\n";
    if (!prompt_for_ipv4("Bind IPv4 (default: 127.0.0.1): ", "127.0.0.1", plan.host))
    {
        logger.error("Setup aborted while reading the bind IP");
        return 1;
    }
    return 0;
}

int prompt_tls_mode(SetupPlan& plan, const std::string& absolute_config_path,
                    bool non_loopback, Logger& logger)
{
    if (!non_loopback)
        return 0;

    bool use_self_signed = true;
    if (!prompt_yes_no("Generate self-signed TLS certificate? [y/n] (default: yes): ", true,
                       use_self_signed))
    {
        logger.error("Setup aborted while choosing TLS certificate mode");
        return 1;
    }

    if (use_self_signed)
    {
        plan.tls_mode = SetupTlsMode::SelfSigned;
        return 0;
    }

    const std::string config_dir = dir_of(absolute_config_path);
    if (!prompt_for_existing_tls_paths(config_dir, plan.tls_cert_path, plan.tls_key_path))
    {
        logger.error("Setup aborted while reading TLS file paths");
        return 1;
    }
    plan.tls_mode = SetupTlsMode::ExistingFiles;
    return 0;
}

int prompt_api_key_choices(SetupPlan& plan, bool non_loopback, Logger& logger)
{
    const std::string admin_prompt = non_loopback
        ? "Generate admin API key? [y/n] (default: yes, required for non-loopback bind): "
        : "Generate admin API key? [y/n] (default: yes): ";
    if (!prompt_yes_no(admin_prompt, true, plan.generate_admin_key))
    {
        logger.error("Setup aborted while handling the admin API key");
        return 1;
    }
    if (non_loopback && !plan.generate_admin_key)
    {
        logger.error("Non-loopback binding requires an admin API key; setup aborted");
        return 1;
    }
    if (!prompt_yes_no("Generate restricted user API key? [y/n] (default: no): ", false,
                       plan.generate_user_key))
    {
        logger.error("Setup aborted while handling the user API key");
        return 1;
    }
    return 0;
}

void print_setup_result(const SetupResult& result, const std::string& absolute_config_path)
{
    std::cout << "Wrote config: " << absolute_config_path << "\n";
    if (!result.admin_api_key.empty() || !result.user_api_key.empty())
        std::cout << "API keys are shown once below — save them now, they cannot be recovered:\n";
    if (!result.admin_api_key.empty())
        std::cout << "  Admin API key:           " << result.admin_api_key << "\n";
    if (!result.user_api_key.empty())
        std::cout << "  Restricted user API key: " << result.user_api_key << "\n";
}
} // namespace

int ApplicationBootstrap::run_setup(const std::string& config_path, Logger& logger)
{
    logger.set_level(LogLevel::Error);

    const int tty_rc = check_tty_or_fail();
    if (tty_rc != 0)
        return tty_rc;

    const std::string absolute_config_path = to_absolute_path(config_path);

    const int overwrite_rc = confirm_overwrite_or_fail(absolute_config_path, logger);
    if (overwrite_rc != 0)
        return overwrite_rc;

    SetupPlan plan;
    plan.port = make_setup_persisted_config().port;

    const int host_rc = prompt_host_into_plan(plan, absolute_config_path, logger);
    if (host_rc != 0)
        return host_rc;

    const bool non_loopback = !is_loopback_host(plan.host);

    const int tls_rc = prompt_tls_mode(plan, absolute_config_path, non_loopback, logger);
    if (tls_rc != 0)
        return tls_rc;

    const int key_rc = prompt_api_key_choices(plan, non_loopback, logger);
    if (key_rc != 0)
        return key_rc;

    const SetupResult result = apply_setup_plan(plan, config_path, logger);
    if (!result.ok)
    {
        logger.error("Setup failed: " + result.error);
        return 1;
    }

    print_setup_result(result, absolute_config_path);
    return 0;
}
