#include "service/profile_service.hpp"
#include "db/write_transaction.hpp"
#include "scan/scan_request_validator.hpp"
#include "scan/scan_types.hpp"
#include <cctype>
#include <stdexcept>

namespace
{

const ProfileWriteRequest DEFAULT_PROFILES[] = {
    {"localhost", "127.0.0.1", "1-128", false},
    {"LAN sweep", "192.168.1.0/24", "22,80,443", false},
    {"single host", "192.168.1.10", "22,80,443,8080", false},
    {"discovery only", "192.168.0.0/16", "", true}
};

bool has_visible_character(const std::string& value)
{
    for (std::string::const_iterator it = value.begin(); it != value.end(); ++it)
    {
        if (!std::isspace(static_cast<unsigned char>(*it)))
            return true;
    }
    return false;
}

bool contains_null_byte(const std::string& value)
{
    return value.find('\0') != std::string::npos;
}

bool contains_control_characters(const std::string& value)
{
    for (std::string::const_iterator it = value.begin(); it != value.end(); ++it)
    {
        const unsigned char ch = static_cast<unsigned char>(*it);
        if (ch < 0x20 || ch == 0x7F)
            return true;
    }
    return false;
}

ProfileWriteError validate_profile_name(const std::string& name)
{
    if (!has_visible_character(name))
        return ProfileWriteError::NameEmpty;
    if (contains_null_byte(name))
        return ProfileWriteError::NameNullByte;
    if (contains_control_characters(name))
        return ProfileWriteError::NameControlChar;
    if (name.size() > MAX_PROFILE_NAME_LENGTH)
        return ProfileWriteError::NameTooLong;
    return ProfileWriteError::None;
}

ProfileWriteError validate_profile_input(ProfileWriteRequest& request)
{
    const ProfileWriteError name_error = validate_profile_name(request.name);
    if (name_error != ProfileWriteError::None)
        return name_error;

    const ScanRequestValidation validation = validate_scan_request(
        {request.target, request.ports, request.host_discovery_only, ""});
    if (!validation.ok)
        return ProfileWriteError::InvalidData;

    request.target = validation.request.target;
    request.ports = validation.request.ports;
    return ProfileWriteError::None;
}

void seed_default_profiles(sqlite3* h, ProfileRepository& repo)
{
    for (ProfileWriteRequest request : DEFAULT_PROFILES)
    {
        if (validate_profile_input(request) != ProfileWriteError::None)
            throw std::logic_error("invalid default scan profile: " + request.name);
        if (repo.create_profile(h, request) <= 0)
            throw std::runtime_error("failed to seed default scan profile: " + request.name);
    }
}

} // namespace

ProfileService::ProfileService(Database& db, ScanService& scan_service)
    : db_(db), scan_service_(scan_service), repo_(db)
{
}

std::vector<ScanProfile> ProfileService::list_profiles()
{
    db_.write([this](sqlite3* h)
    {
        if (repo_.are_default_profiles_seeded(h))
            return;

        WriteTransaction tx(h);
        const std::vector<ScanProfile> profiles = repo_.list_profiles(h);
        if (profiles.empty())
            seed_default_profiles(h, repo_);
        if (!repo_.mark_default_profiles_seeded(h))
            throw std::runtime_error("failed to mark default scan profiles as seeded");
        tx.commit();
    });

    return db_.read([this](sqlite3* h) { return repo_.list_profiles(h); });
}

ProfileWriteResult ProfileService::create_profile(const ProfileWriteRequest& request)
{
    ProfileWriteRequest normalized = request;
    const ProfileWriteError error = validate_profile_input(normalized);
    if (error != ProfileWriteError::None)
        return ProfileWriteResult{error, nullptr};

    ProfileWriteResult result;
    result.profile = db_.write(
        [this, &normalized, &result](sqlite3* h) -> std::unique_ptr<ScanProfile>
        {
            if (repo_.profile_name_exists(h, normalized.name))
            {
                result.error = ProfileWriteError::NameConflict;
                return nullptr;
            }
            WriteTransaction tx(h);
            const int id = repo_.create_profile(h, normalized);
            if (id <= 0)
                return nullptr;
            std::unique_ptr<ScanProfile> profile = repo_.get_profile(h, id);
            tx.commit();
            return profile;
        });
    if (!result.profile && result.error == ProfileWriteError::None)
        result.error = ProfileWriteError::InvalidData;
    return result;
}

ProfileWriteResult ProfileService::update_profile(int id, const ProfileWriteRequest& request)
{
    ProfileWriteRequest normalized = request;
    const ProfileWriteError error = validate_profile_input(normalized);
    if (error != ProfileWriteError::None)
        return ProfileWriteResult{error, nullptr};

    ProfileWriteResult result;
    result.profile = db_.write(
        [this, id, &normalized, &result](sqlite3* h) -> std::unique_ptr<ScanProfile>
        {
            if (repo_.profile_name_exists(h, normalized.name, id))
            {
                result.error = ProfileWriteError::NameConflict;
                return nullptr;
            }
            WriteTransaction tx(h);
            if (!repo_.update_profile(h, id, normalized))
                return nullptr;
            std::unique_ptr<ScanProfile> profile = repo_.get_profile(h, id);
            tx.commit();
            return profile;
        });
    if (!result.profile && result.error == ProfileWriteError::None)
        result.error = ProfileWriteError::NotFound;
    return result;
}

bool ProfileService::delete_profile(int id)
{
    return db_.write([this, id](sqlite3* h) { return repo_.delete_profile(h, id); });
}

ScanService::StartAsyncResult ProfileService::run_profile(int id)
{
    std::unique_ptr<ScanProfile> profile =
        db_.read([this, id](sqlite3* h) { return repo_.get_profile(h, id); });

    if (!profile)
    {
        ScanService::StartAsyncResult result;
        result.status        = ScanService::StartAsyncStatus::ValidationError;
        result.error_message = "profile not found";
        return result;
    }

    ScanRequest request;
    request.target              = profile->target;
    request.ports               = profile->ports;
    request.host_discovery_only = profile->host_discovery_only;
    return scan_service_.start_async(request);
}
