#ifndef DB_PROFILE_REPOSITORY_HPP
#define DB_PROFILE_REPOSITORY_HPP

#include "db/database.hpp"
#include <memory>
#include <string>
#include <vector>

struct ScanProfile
{
    int         id = 0;
    std::string name;
    std::string target;
    std::string ports;
    bool        host_discovery_only = false;
    std::string created_at;
};

struct ProfileWriteRequest
{
    std::string name;
    std::string target;
    std::string ports;
    bool        host_discovery_only = false;
};

// Repository for saved scan profiles.
// REQUIRES: caller passes the sqlite3* handle from the enclosing
// Database::read()/write() access scope.
class ProfileRepository
{
public:
    explicit ProfileRepository(Database& db);

    std::vector<ScanProfile> list_profiles(sqlite3* h);
    std::unique_ptr<ScanProfile> get_profile(sqlite3* h, int id);
    bool profile_name_exists(sqlite3* h, const std::string& name, int exclude_id = 0);
    int create_profile(sqlite3* h, const ProfileWriteRequest& req);
    bool update_profile(sqlite3* h, int id, const ProfileWriteRequest& req);
    bool delete_profile(sqlite3* h, int id);
    bool are_default_profiles_seeded(sqlite3* h);
    bool mark_default_profiles_seeded(sqlite3* h);
};

#endif
