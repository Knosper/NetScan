#include "app/startup_validation.hpp"

#include "util/path_utils.hpp"

#include <cerrno>
#include <cstring>

namespace
{
std::string describe_errno(const std::string& prefix, const std::string& path)
{
    return prefix + ": " + path + " (" + std::strerror(errno) + ")";
}

void add_issue(StartupValidationResult& result, bool fatal, const std::string& message)
{
    result.issues.push_back({fatal, message});
    if (fatal)
        result.ok = false;
}

void validate_web_dir(const AppConfig& config, StartupValidationResult& result)
{
    if (!path_exists(config.web_dir))
    {
        add_issue(result, true, describe_errno("web_dir does not exist", config.web_dir));
        return;
    }

    if (!is_directory(config.web_dir))
    {
        add_issue(result, true, "web_dir is not a directory: " + config.web_dir);
        return;
    }

    if (!is_readable(config.web_dir))
    {
        add_issue(result, true, describe_errno("web_dir is not readable", config.web_dir));
        return;
    }

    const std::string index_path = join_path(config.web_dir, "index.html");
    if (!path_exists(index_path))
    {
        add_issue(result, true, describe_errno("web entrypoint is missing", index_path));
        return;
    }

    if (!is_regular_file(index_path))
    {
        add_issue(result, true, "web entrypoint is not a regular file: " + index_path);
        return;
    }

    if (!is_readable(index_path))
        add_issue(result, true, describe_errno("web entrypoint is not readable", index_path));
}

void validate_db_path(const AppConfig& config, StartupValidationResult& result)
{
    const std::string db_dir = dir_of(config.db_path);
    if (!path_exists(db_dir))
    {
        add_issue(result, true, describe_errno("database parent directory does not exist", db_dir));
        return;
    }

    if (!is_directory(db_dir))
    {
        add_issue(result, true, "database parent path is not a directory: " + db_dir);
        return;
    }

    if (!is_writable(db_dir))
        add_issue(result, true, describe_errno("database parent directory is not writable", db_dir));
}

void validate_tls_config(const AppConfig& config, StartupValidationResult& result)
{
    auto check_file = [&](const std::string& path, const char* label)
    {
        if (!path_exists(path))
        {
            add_issue(result, true, describe_errno(std::string(label) + " does not exist", path));
            return;
        }
        if (!is_regular_file(path))
        {
            add_issue(result, true, std::string(label) + " is not a regular file: " + path);
            return;
        }
        if (!is_readable(path))
            add_issue(result, true, describe_errno(std::string(label) + " is not readable", path));
    };

    check_file(config.tls_cert_path, "tls_cert_path");
    check_file(config.tls_key_path, "tls_key_path");
}

std::string format_nmap_issue(const NmapCheckResult& nmap_result)
{
    if (nmap_result.status == HealthCheckStatus::Ok)
        return "nmap available";
    if (nmap_result.status == HealthCheckStatus::Missing)
        return "nmap not found in PATH; scan functionality is degraded";
    if (nmap_result.detail.empty())
        return "nmap availability check returned an unknown degraded state";
    return "nmap availability check degraded: " + nmap_result.detail;
}
} // namespace

StartupValidationResult validate_startup_environment(const AppConfig& config, Logger& logger,
                                                     NmapCheckFn nmap_check_fn)
{
    StartupValidationResult result;
    result.ok = true;

    if (config.ui_enabled)
        validate_web_dir(config, result);

    validate_db_path(config, result);

    if (config.tls_enabled)
        validate_tls_config(config, result);

    logger.info("Startup validation: ui_enabled=" + std::string(config.ui_enabled ? "true" : "false"));

    const NmapCheckResult nmap_result = nmap_check_fn ? nmap_check_fn() : check_nmap();
    const std::string nmap_message = format_nmap_issue(nmap_result);
    if (nmap_result.status == HealthCheckStatus::Ok)
        logger.info("Startup validation: " + nmap_message);
    else
    {
        logger.warn("Startup validation: " + nmap_message);
        result.issues.push_back({false, nmap_message});
    }

    return result;
}
