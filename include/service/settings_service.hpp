#ifndef SERVICE_SETTINGS_SERVICE_HPP
#define SERVICE_SETTINGS_SERVICE_HPP

#include "app/config.hpp"
#include "db/allowed_target_repository.hpp"
#include "db/database.hpp"
#include "util/logger.hpp"
#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

class SettingsService
{
public:
    static constexpr std::size_t kMaxUserAllowedTargets = 256U;
    // Max valid IPv4 CIDR is much shorter; keep a generous safety buffer.
    static constexpr std::size_t kMaxUserAllowedTargetLength = 64U;

    struct Settings
    {
        std::string log_level;
        int scan_cooldown_seconds = 0;
        std::vector<std::string> user_allowed_targets;
    };

    enum class SaveStatus
    {
        Saved,
        BadRequest,
        Error
    };

    struct SaveResult
    {
        SaveStatus status = SaveStatus::Error;
        std::string error;
        std::vector<std::string> warnings;
    };

    SettingsService(Database& db, const std::string& config_path, Logger& logger);

    Settings load_settings() const;
    std::vector<std::string> list_allowed_targets() const;
    SaveResult save_settings(const Settings& settings);

    // Marks conf.ini as pending setup with the given port for the setup server.
    // Returns false (and sets error) on validation failure or write error.
    bool request_setup(int setup_port, std::string& error);

private:
    Database& db_;
    AllowedTargetRepository repo_;
    std::string config_path_;
    Logger& logger_;

    // In-memory snapshot of the persisted AppSettings. Loaded once at startup
    // and updated on save_settings(); all reads go through this snapshot so
    // runtime callers never re-read conf.ini from disk.
    mutable std::mutex cache_mutex_;
    AppSettings cached_settings_;

    bool validate_allowed_targets(const std::vector<std::string>& targets,
                                  std::string& error) const;
};

#endif
