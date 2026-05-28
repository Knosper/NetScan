#ifndef DB_ALLOWED_TARGET_REPOSITORY_HPP
#define DB_ALLOWED_TARGET_REPOSITORY_HPP

#include "db/database.hpp"

#include <string>
#include <vector>

// REQUIRES: caller passes the sqlite3* handle from the enclosing
// Database::read()/write() access scope.
class AllowedTargetRepository
{
public:
    explicit AllowedTargetRepository(Database& db);

    std::vector<std::string> list(sqlite3* h) const;
    bool add(sqlite3* h, const std::string& cidr);
    bool remove(sqlite3* h, const std::string& cidr);
    bool clear(sqlite3* h);
    bool replace_all(sqlite3* h, const std::vector<std::string>& cidrs);
};

#endif
