#ifndef SERVICE_PROFILE_SERVICE_HPP
#define SERVICE_PROFILE_SERVICE_HPP

#include "db/database.hpp"
#include "db/profile_repository.hpp"
#include "service/scan_service.hpp"
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

constexpr std::size_t MAX_PROFILE_NAME_LENGTH = 128U;

enum class ProfileWriteError
{
    None,
    NameEmpty,
    NameTooLong,
    NameNullByte,
    NameControlChar,
    NameConflict,
    InvalidData,
    NotFound
};

struct ProfileWriteResult
{
    ProfileWriteError             error = ProfileWriteError::None;
    std::unique_ptr<ScanProfile>  profile;
};

class ProfileService
{
public:
    ProfileService(Database& db, ScanService& scan_service);

    std::vector<ScanProfile> list_profiles();

    ProfileWriteResult create_profile(const std::string& name,
                                      const std::string& target,
                                      const std::string& ports,
                                      bool               host_discovery_only);
    ProfileWriteResult update_profile(int id, const std::string& name,
                                      const std::string& target,
                                      const std::string& ports,
                                      bool               host_discovery_only);

    bool delete_profile(int id);

    ScanService::StartAsyncResult run_profile(int id);

private:
    Database&          db_;
    ScanService&       scan_service_;
    ProfileRepository  repo_;
};

#endif
