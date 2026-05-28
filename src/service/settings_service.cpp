#include "service/settings_service.hpp"

#include "scan/target_validation.hpp"
#include "util/string_utils.hpp"

#include <cstdio>
#include <set>

namespace
{
std::string make_allowed_target_field_name(std::size_t index)
{
    return "field 'user_allowed_targets[" + std::to_string(index) + "]'";
}

bool validate_allowed_target_entry(const std::string& target, std::size_t index,
                                   std::set<std::string>& seen_targets,
                                   std::string& error)
{
    const std::string field_name = make_allowed_target_field_name(index);
    const std::string trimmed = util::trim(target);
    if (trimmed.empty())
    {
        error = field_name + " must not be empty";
        return false;
    }
    if (trimmed.size() > SettingsService::kMaxUserAllowedTargetLength)
    {
        error = field_name + " must be at most " +
                std::to_string(SettingsService::kMaxUserAllowedTargetLength) + " characters";
        return false;
    }

    const std::vector<std::string> allowed_targets(1, trimmed);
    if (!is_user_target_allowed(trimmed, allowed_targets))
    {
        error = field_name + " must be an IP or CIDR target";
        return false;
    }

    ParsedCidr parsed;
    if (!try_parse_ip_or_cidr(trimmed, parsed) || !parsed.is_ipv4)
    {
        error = field_name + " IPv6 entries are not allowed";
        return false;
    }

    const std::string canonical = canonicalize_target(trimmed);
    if (!seen_targets.insert(canonical).second)
    {
        error = field_name + " duplicates an earlier entry";
        return false;
    }

    return true;
}
}

SettingsService::SettingsService(Database& db, const std::string& config_path, Logger& logger)
    : db_(db), repo_(db), config_path_(config_path), logger_(logger),
      cached_settings_(load_raw_settings(config_path))
{
}

SettingsService::Settings SettingsService::load_settings() const
{
    Settings settings;
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        settings.log_level = cached_settings_.log_level;
        settings.scan_cooldown_seconds = cached_settings_.scan_cooldown_seconds;
    }
    settings.user_allowed_targets = db_.read(
        [this](sqlite3* h)
        {
            return repo_.list(h);
        });
    return settings;
}

std::vector<std::string> SettingsService::list_allowed_targets() const
{
    return db_.read(
        [this](sqlite3* h)
        {
            return repo_.list(h);
        });
}

bool SettingsService::validate_allowed_targets(const std::vector<std::string>& targets,
                                               std::string& error) const
{
    if (targets.size() > kMaxUserAllowedTargets)
    {
        error = "field 'user_allowed_targets' must contain at most " +
                std::to_string(kMaxUserAllowedTargets) + " entries";
        return false;
    }

    std::set<std::string> seen_targets;
    for (std::size_t i = 0; i < targets.size(); ++i)
    {
        if (!validate_allowed_target_entry(targets[i], i, seen_targets, error))
            return false;
    }

    return true;
}

SettingsService::SaveResult SettingsService::save_settings(const Settings& settings)
{
    std::string error;
    AppSettings config_settings;
    config_settings.log_level = settings.log_level;
    config_settings.scan_cooldown_seconds = settings.scan_cooldown_seconds;
    if (!validate_settings(config_settings, error))
        return {SaveStatus::BadRequest, error, {}};
    if (!validate_allowed_targets(settings.user_allowed_targets, error))
        return {SaveStatus::BadRequest, error, {}};

    if (!write_settings_file_atomically(config_path_, config_settings, error))
    {
        logger_.error("failed to write settings file '" + config_path_ + "': " + error);
        return {SaveStatus::Error, error, {}};
    }

    const bool persisted = db_.write(
        [this, &settings](sqlite3* h)
        {
            return repo_.replace_all(h, settings.user_allowed_targets);
        });
    if (!persisted)
    {
        logger_.error("failed to persist user_allowed_targets in database");
        return {SaveStatus::Error, "failed to persist user_allowed_targets", {}};
    }

    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        cached_settings_ = config_settings;
    }
    logger_.set_level(parse_log_level(settings.log_level));
    logger_.info("settings saved to " + config_path_ + "; log level applied live");
    return {SaveStatus::Saved, "", {}};
}

bool SettingsService::request_setup(int setup_port, std::string& error)
{
    if (setup_port < 1 || setup_port > 65535)
    {
        error = "setup port must be between 1 and 65535";
        return false;
    }

    PersistedConfig pending = make_default_persisted_config();
    pending.port = setup_port;
    pending.setup_pending = true;

    if (!write_persisted_config_atomically(config_path_, pending, error))
    {
        logger_.error("failed to mark config as setup-pending: " + error);
        return false;
    }
    logger_.info("config marked as setup-pending; setup server will bind 127.0.0.1:" +
                 std::to_string(setup_port));
    return true;
}
