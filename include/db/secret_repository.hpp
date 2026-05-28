#ifndef DB_SECRET_REPOSITORY_HPP
#define DB_SECRET_REPOSITORY_HPP

#include "db/database.hpp"
#include <string>

// REQUIRES: caller passes the sqlite3* handle from the enclosing
// Database::read()/write() access scope.
class SecretRepository
{
public:
    explicit SecretRepository(Database& db);

    std::string load_hash(sqlite3* h, const std::string& name);
    bool upsert_hash(sqlite3* h, const std::string& name, const std::string& hash);
};

#endif
