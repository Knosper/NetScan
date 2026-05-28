#ifndef DB_HOST_META_REPOSITORY_HPP
#define DB_HOST_META_REPOSITORY_HPP

#include "db/database.hpp"
#include "host/host_types.hpp"
#include <string>

// REQUIRES: caller passes the sqlite3* handle from the enclosing
// Database::read()/write() access scope.
class HostMetaRepository
{
public:
    explicit HostMetaRepository(Database& db);

    HostMeta get_meta(sqlite3* h, const std::string& ip);
    void upsert_meta(sqlite3* h, const std::string& ip, const HostMeta& meta);
    void clear_field(sqlite3* h, const std::string& ip, const std::string& field);
};

#endif
