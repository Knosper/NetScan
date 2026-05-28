#include "app/db_bootstrap.hpp"

#include "app/logging_setup.hpp"
#include "app/setup_bootstrap.hpp"
#include "app/startup_validation.hpp"
#include "db/secret_repository.hpp"
#include "db/schema.hpp"
#include "scan/target_validation.hpp"
#include "util/secret_utils.hpp"
#include <iostream>
#include <stdexcept>

AuthConfig load_auth_config(Database& db)
{
    SecretRepository repo(db);
    return db.read(
        [&repo](sqlite3* h)
        {
            AuthConfig auth_config;
            auth_config.admin_api_key_hash = repo.load_hash(h, admin_secret_name());
            auth_config.restricted_api_key_hash = repo.load_hash(h, restricted_secret_name());
            return auth_config;
        });
}

bool admin_api_key_exists(Database& db)
{
    return !load_auth_config(db).admin_api_key_hash.empty();
}

bool persist_generated_api_key(SecretRepository& repo, sqlite3* h, const char* secret_name,
                               std::string& plaintext_out)
{
    plaintext_out = generate_secret_key();
    return repo.upsert_hash(h, secret_name, hash_secret_key(plaintext_out));
}

AppConfig load_and_configure(const std::string& config_path, const CliOverrides& overrides, Logger& logger)
{
    logger.info("Starting NetScan server");
    logger.info("Using config: " + config_path);

    const AppConfig config = apply_cli_overrides(load_config(config_path, logger), overrides);
    configure_logging(config, logger, std::cerr);
    logger.info("nmap executable: " +
                (config.nmap_path.empty() ? std::string("(PATH lookup)") : config.nmap_path));
    return config;
}

void validate_startup(const AppConfig& config, const AuthConfig& auth_config,
                             Logger& logger)
{
    const StartupValidationResult startup_validation = validate_startup_environment(config, logger);
    for (const auto& issue : startup_validation.issues)
    {
        if (issue.fatal)
            logger.error("Startup validation failed: " + issue.message);
        else
            logger.warn("Startup validation degraded: " + issue.message);
    }
    if (!startup_validation.ok)
        throw std::runtime_error("Startup validation failed");

    if (!auth_config.restricted_api_key_hash.empty() && auth_config.admin_api_key_hash.empty())
        throw std::runtime_error(
            "Startup auth error: restricted API key is configured but no admin API key exists. "
            "Run './netscan --generate-key admin' first.");

    if (!is_loopback_host(config.host) && auth_config.admin_api_key_hash.empty())
        throw std::runtime_error(
            "Startup auth error: non-loopback binding requires an admin API key; "
            "run './netscan --generate-key admin' first");

    if (!is_loopback_host(config.host) && !config.tls_enabled)
        throw std::runtime_error(
            "Startup error: non-loopback binding requires TLS; "
            "set tls_enabled=true and configure tls_cert_path/tls_key_path in conf.ini. "
            "See docs/tls-setup.md");

    if (!is_loopback_host(config.host) && config.tls_enabled)
        logger.info("TLS enabled for network binding on " + config.host + ":" +
                    std::to_string(config.port));
}

void initialize_database(Database& db, const AppConfig& config, Logger& logger)
{
    logger.info("Database opened at: " + config.db_path);
    configure_database_runtime(db, logger);
    init_schema(db, logger);
    harden_database_file_permissions(db, logger);
}

bool persist_api_key_hash(const AppConfig& config, const std::string& secret_name,
                          const std::string& hash, Logger& logger)
{
    Database db(config.db_path);
    initialize_database(db, config, logger);

    SecretRepository repo(db);
    return db.write([&repo, &secret_name, &hash](sqlite3* h) { return repo.upsert_hash(h, secret_name, hash); });

}
