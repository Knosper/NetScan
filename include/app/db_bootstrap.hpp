#ifndef APP_DB_BOOTSTRAP_HPP
#define APP_DB_BOOTSTRAP_HPP

#include "app/auth_config.hpp"
#include "app/config.hpp"
#include "db/database.hpp"
#include "db/secret_repository.hpp"
#include "util/logger.hpp"
#include <sqlite3.h>
#include <string>

AppConfig load_and_configure(const std::string& config_path, const CliOverrides& overrides,
                             Logger& logger);
void initialize_database(Database& db, const AppConfig& config, Logger& logger);
AuthConfig load_auth_config(Database& db);
bool admin_api_key_exists(Database& db);
bool persist_generated_api_key(SecretRepository& repo, sqlite3* h, const char* secret_name,
                               std::string& plaintext_out);
void validate_startup(const AppConfig& config, const AuthConfig& auth_config, Logger& logger);
bool persist_api_key_hash(const AppConfig& config, const std::string& secret_name,
                          const std::string& hash, Logger& logger);

#endif
