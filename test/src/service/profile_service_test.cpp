#include "db/database.hpp"
#include "db/profile_repository.hpp"
#include "db/schema.hpp"
#include "scan/scan_request_validator.hpp"
#include "scan_service_test_support.hpp"
#include "service/profile_service.hpp"
#include "service/scheduler_service.hpp"
#include "test_output.hpp"

#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace
{
struct Services
{
    ScanService& scans;
    ProfileService& profiles;
    SchedulerService& scheduler;
};

ScheduledJob make_job(const ScanRequest& request)
{
    ScheduledJob job;
    job.target = request.target;
    job.ports = request.ports;
    job.host_discovery_only = request.host_discovery_only;
    job.interval_seconds = 60;
    job.enabled = false;
    return job;
}

bool check_seeded_profiles(Services& services)
{
    bool ok = true;
    const auto profiles = services.profiles.list_profiles();
    ok = expect(profiles.size() == 4, "fresh database should contain four default profiles") && ok;
    for (const ScanProfile& profile : profiles)
    {
        const auto result = services.profiles.run_profile(profile.id);
        ok = expect(result.status == ScanService::StartAsyncStatus::DependencyMissing,
                    "every default profile should pass scan validation: " + profile.name) && ok;
    }
    ok = expect(services.profiles.list_profiles().size() == profiles.size(),
                "reading default profiles twice should not duplicate them") && ok;
    return ok;
}

bool check_rejected_request(Services& services, const ScanRequest& request, int profile_id)
{
    const auto validation = validate_scan_request(request);
    const auto scan = services.scans.start_async(request);
    const ProfileWriteRequest profile{"rejected", request.target, request.ports,
                                      request.host_discovery_only};
    const auto created = services.profiles.create_profile(profile);
    const auto updated = services.profiles.update_profile(profile_id, profile);
    const int job_id = services.scheduler.create_job(make_job(request));
    bool ok = expect(!validation.ok, "invalid request should fail shared validation");
    ok = expect(scan.status == ScanService::StartAsyncStatus::ValidationError &&
                scan.error_message == validation.error_message,
                "scan start should return the shared validation error") && ok;
    ok = expect(created.error == ProfileWriteError::InvalidData && !created.profile,
                "profile creation should reject the same invalid request") && ok;
    ok = expect(updated.error == ProfileWriteError::InvalidData && !updated.profile,
                "profile update should reject the same invalid request") && ok;
    ok = expect(job_id == 0, "scheduler should reject the same invalid request") && ok;
    return ok;
}

bool check_rejected_requests(Services& services)
{
    const auto existing = services.profiles.list_profiles();
    if (existing.empty())
        return expect(false, "validation fixture needs an existing profile");
    const ScanRequest requests[] = {
        {"", "80", false, ""}, {"127.0.0.1/33", "80", false, ""},
        {"127.0.0.1", "80", true, ""}, {"127.0.0.1", "0", false, ""},
        {"127.0.0.1", "1-129", false, ""}
    };
    bool ok = true;
    for (const ScanRequest& request : requests)
        ok = check_rejected_request(services, request, existing.front().id) && ok;
    const auto after = services.profiles.list_profiles();
    return expect(after.size() == existing.size() && after.front().name == existing.front().name,
                  "rejected writes should preserve existing profiles") && ok;
}

bool check_normalized_requests(Services& services)
{
    const ScanRequest request{"127.0.0.1/32", "22, 80", false, ""};
    const auto validation = validate_scan_request(request);
    const auto profile = services.profiles.create_profile(
        {"normalized", request.target, request.ports, false});
    const int job_id = services.scheduler.create_job(make_job(request));
    const auto job = services.scheduler.get_job(job_id);
    bool ok = expect(validation.ok && profile.profile && job,
                     "valid requests should be accepted by profiles and scheduler");
    if (!ok)
        return false;
    ok = expect(profile.profile->target == validation.request.target &&
                profile.profile->ports == "22,80" && job->target == validation.request.target &&
                job->ports == validation.request.ports,
                "profile and scheduler should persist the same normalized scan request") && ok;
    const auto updated = services.profiles.update_profile(
        profile.profile->id, {"normalized", "127.0.0.1", "80, 443", false});
    return expect(updated.profile && updated.profile->ports == "80,443",
                  "profile update should use the same port normalization") && ok;
}

bool check_seed_rollback(Database& db, ProfileService& profiles)
{
    db.write([](sqlite3* h) {
        return sqlite3_exec(h, "DELETE FROM scan_profiles;"
            "DELETE FROM app_metadata WHERE key='default_scan_profiles_seeded';"
            "CREATE TRIGGER reject_default_profile BEFORE INSERT ON scan_profiles "
            "WHEN NEW.name='LAN sweep' BEGIN SELECT RAISE(ABORT, 'test failure'); END;",
            nullptr, nullptr, nullptr);
    });
    bool threw = false;
    std::size_t listed = 1;
    try { listed = profiles.list_profiles().size(); }
    catch (const std::exception&) { threw = true; }
    const bool rolled_back = db.read([&db](sqlite3* h) {
        ProfileRepository repo(db);
        return repo.list_profiles(h).empty() && !repo.are_default_profiles_seeded(h);
    });
    db.write([](sqlite3* h) {
        return sqlite3_exec(h, "DROP TRIGGER reject_default_profile;", nullptr, nullptr, nullptr);
    });
    return expect(!threw && listed == 0, "failed seed should still return the profile list") &&
           expect(rolled_back, "failed seed should roll back rows and seeded marker") &&
           expect(profiles.list_profiles().size() == 4, "failed seed should be safely retryable");
}

bool check_marker_failure(Database& db, ProfileService& profiles)
{
    db.write([](sqlite3* h) {
        return sqlite3_exec(h, "DELETE FROM scan_profiles;"
            "DELETE FROM app_metadata WHERE key='default_scan_profiles_seeded';"
            "CREATE TRIGGER reject_seed_marker BEFORE INSERT ON app_metadata "
            "WHEN NEW.key='default_scan_profiles_seeded' "
            "BEGIN SELECT RAISE(ABORT, 'test failure'); END;",
            nullptr, nullptr, nullptr);
    });
    bool threw = false;
    std::size_t listed = 0;
    try { listed = profiles.list_profiles().size(); }
    catch (const std::exception&) { threw = true; }
    db.write([](sqlite3* h) {
        return sqlite3_exec(h, "DROP TRIGGER reject_seed_marker;", nullptr, nullptr, nullptr);
    });
    const std::size_t retried = profiles.list_profiles().size();
    const bool marked = db.read([&db](sqlite3* h) {
        return ProfileRepository(db).are_default_profiles_seeded(h);
    });
    return expect(!threw && listed == 4, "failed seed marker should not fail the profile list") &&
           expect(retried == 4 && marked, "marker retry should not duplicate default profiles");
}

int read_user_version(Database& db)
{
    return db.read([](sqlite3* h) {
        int version = -1;
        sqlite3_exec(h, "PRAGMA user_version;",
            [](void* out, int, char** values, char**) {
                *static_cast<int*>(out) = values[0] ? std::atoi(values[0]) : -1;
                return 0;
            }, &version, nullptr);
        return version;
    });
}

bool check_base_schema_version(Database& db, Logger& logger)
{
    bool ok = expect(read_user_version(db) == 1, "base schema should set user_version 1");
    init_schema(db, logger);
    ok = expect(read_user_version(db) == 1, "re-running init_schema should keep user_version") && ok;

    db.write([](sqlite3* h) { sqlite3_exec(h, "PRAGMA user_version = 2;", nullptr, nullptr, nullptr); });
    bool threw = false;
    try
    {
        init_schema(db, logger);
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }
    db.write([](sqlite3* h) { sqlite3_exec(h, "PRAGMA user_version = 1;", nullptr, nullptr, nullptr); });
    return expect(threw, "a database from a newer schema version should be rejected") && ok;
}
} // namespace

int main()
{
    const std::string path = make_temp_db_path("/tmp/netscan-profile-service-XXXXXX.db");
    if (path.empty())
        return finish_test("profile_service_test", false);
    bool ok = true;
    {
        Logger logger;
        Database db(path);
        configure_database_runtime(db, logger);
        init_schema(db, logger);
        ScanService scans(db, logger);
        scans.set_nmap_path(path + ".missing-nmap");
        ProfileService profiles(db, scans);
        SchedulerService scheduler(db, scans, logger);
        Services services{scans, profiles, scheduler};
        ok = check_seeded_profiles(services) && ok;
        ok = check_rejected_requests(services) && ok;
        ok = check_normalized_requests(services) && ok;
        ok = check_seed_rollback(db, profiles) && ok;
        ok = check_marker_failure(db, profiles) && ok;
        ok = check_base_schema_version(db, logger) && ok;
    }
    std::remove(path.c_str());
    return finish_test("profile_service_test", ok);
}
