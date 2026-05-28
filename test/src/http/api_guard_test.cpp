#include "test_output.hpp"
#include "util/secret_utils.hpp"
#include "http/http_status.hpp"

#include <chrono>
#include <string>

#include "../../../src/http/api_guard.cpp"

namespace
{
bool expect_true(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << "\n";
        return false;
    }
    return true;
}

httplib::Request make_req(const std::string& method, const std::string& path)
{
    httplib::Request req;
    req.method = method;
    req.path   = path;
    return req;
}

using HR = httplib::Server::HandlerResponse;

HR call_guard(RateLimiter& rl, const std::string& admin_hash,
              const std::string& restricted_hash, bool open_mode,
              httplib::Request& req, httplib::Response& res)
{
    return apply_auth_guard(rl, admin_hash, restricted_hash, open_mode, req, res);
}

bool unauth_bucket_exhausts_after_max_tokens()
{
    bool ok = true;

    RateLimiter limiter;
    limiter.unauth_bucket.tokens = 1.0;

    const auto now = std::chrono::steady_clock::now();

    ok = expect_true(limiter.allow_unauthenticated(now),
                     "last token: unauthenticated request should be allowed") && ok;
    ok = expect_true(!limiter.allow_unauthenticated(now),
                     "empty bucket: unauthenticated request should be rate-limited") && ok;

    return ok;
}

bool auth_bucket_exhausts_after_max_tokens()
{
    bool ok = true;

    RateLimiter limiter;
    limiter.auth_bucket.tokens = 1.0;

    const auto now = std::chrono::steady_clock::now();

    ok = expect_true(limiter.allow_authenticated(now),
                     "last token: authenticated request should be allowed") && ok;
    ok = expect_true(!limiter.allow_authenticated(now),
                     "empty auth bucket: authenticated request should be rate-limited") && ok;

    return ok;
}

bool non_api_route_bypasses_rate_limit()
{
    RateLimiter limiter;
    limiter.unauth_bucket.tokens = 0.0;

    httplib::Request req = make_req("GET", "/index.html");

    // non-api routes bypass the guard entirely; call apply_auth_guard to exercise the path
    httplib::Response res;
    const HR result = call_guard(limiter, "", "", true, req, res);
    return expect_true(result == HR::Unhandled,
                       "non-api route should bypass rate limit");
}

bool open_mode_allows_without_key()
{
    RateLimiter rl;
    httplib::Request  req = make_req("GET", "/api/health");
    httplib::Response res;

    const HR result = call_guard(rl, "some_hash", "", true, req, res);
    return expect_true(result == HR::Unhandled, "open mode: no key should pass");
}

bool protected_mode_no_key_returns_401()
{
    RateLimiter rl;
    httplib::Request  req = make_req("GET", "/api/health");
    httplib::Response res;

    const HR result = call_guard(rl, "some_hash", "", false, req, res);
    return expect_true(result == HR::Handled && res.status == http_status::UNAUTHORIZED,
                       "no key on protected route should return 401");
}

bool correct_admin_key_passes()
{
    const std::string secret = "testkey";
    const std::string hashed = hash_secret_key(secret);

    RateLimiter rl;
    httplib::Request  req = make_req("GET", "/api/health");
    req.set_header("X-API-Key", secret);
    httplib::Response res;

    const HR result = call_guard(rl, hashed, "", false, req, res);
    return expect_true(result == HR::Unhandled, "correct admin key should pass");
}

bool wrong_admin_key_returns_401()
{
    const std::string hashed = hash_secret_key("realkey");

    RateLimiter rl;
    httplib::Request  req = make_req("GET", "/api/health");
    req.set_header("X-API-Key", "wrongkey");
    httplib::Response res;

    const HR result = call_guard(rl, hashed, "", false, req, res);
    return expect_true(result == HR::Handled && res.status == http_status::UNAUTHORIZED,
                       "wrong admin key should return 401");
}

bool dual_headers_returns_400()
{
    RateLimiter rl;
    httplib::Request  req = make_req("GET", "/api/health");
    req.set_header("X-API-Key", "akey");
    req.set_header("X-NetScan-Restricted-Key", "rkey");
    httplib::Response res;

    const HR result = call_guard(rl, "h1", "h2", false, req, res);
    return expect_true(result == HR::Handled && res.status == http_status::BAD_REQUEST,
                       "dual auth headers should return 400");
}

bool restricted_key_on_allowed_route_passes()
{
    const std::string secret = "rkey";
    const std::string hashed = hash_secret_key(secret);

    RateLimiter rl;
    httplib::Request  req = make_req("GET", "/api/health");
    req.set_header("X-NetScan-Restricted-Key", secret);
    httplib::Response res;

    const HR result = call_guard(rl, "", hashed, false, req, res);
    return expect_true(result == HR::Unhandled,
                       "restricted key on allowed GET route should pass");
}

bool restricted_key_on_disallowed_route_returns_403()
{
    const std::string secret = "rkey";
    const std::string hashed = hash_secret_key(secret);

    RateLimiter rl;
    httplib::Request  req = make_req("DELETE", "/api/scans/1");
    req.set_header("X-NetScan-Restricted-Key", secret);
    httplib::Response res;

    const HR result = call_guard(rl, "", hashed, false, req, res);
    return expect_true(result == HR::Handled && res.status == http_status::FORBIDDEN,
                       "restricted key on disallowed route should return 403");
}

bool rate_limit_returns_429()
{
    RateLimiter rl;
    // Exhaust the unauthenticated bucket; open_mode=true means the request is
    // "authenticated" in the auth sense but apply_auth_guard still picks
    // allow_authenticated. Use open_mode=false + no key so unauth_bucket is used.
    rl.unauth_bucket.tokens = 0.0;

    httplib::Request  req = make_req("GET", "/api/health");
    httplib::Response res;

    const HR result = call_guard(rl, "", "", false, req, res);
    return expect_true(result == HR::Handled && res.status == http_status::TOO_MANY_REQUESTS,
                       "exhausted unauth bucket should return 429");
}

bool non_api_route_bypasses_auth()
{
    RateLimiter rl;
    httplib::Request  req = make_req("GET", "/index.html");
    httplib::Response res;

    const HR result = call_guard(rl, "some_hash", "", false, req, res);
    return expect_true(result == HR::Unhandled,
                       "non-api route should bypass auth guard");
}

// Lockout: 5 failed unauthenticated attempts within 60 s trigger a lockout.
// The 6th unauthenticated request must return 429; an authenticated (open mode)
// request must still succeed because it uses auth_bucket which ignores lockout.
bool lockout_blocks_unauth_but_not_auth()
{
    bool ok = true;

    const std::string real_hash = hash_secret_key("realkey");
    RateLimiter rl;

    // Drive 5 failures via wrong key (record_failure called on each 401).
    for (int i = 0; i < 5; ++i)
    {
        httplib::Request  req = make_req("GET", "/api/health");
        req.set_header("X-API-Key", "wrongkey");
        httplib::Response res;
        call_guard(rl, real_hash, "", false, req, res);
    }

    // 6th unauthenticated request (no key, protected mode) → lockout → 429
    {
        httplib::Request  req = make_req("GET", "/api/health");
        httplib::Response res;
        const HR result = call_guard(rl, real_hash, "", false, req, res);
        ok = expect_true(result == HR::Handled && res.status == http_status::TOO_MANY_REQUESTS,
                         "locked-out unauthenticated request should return 429") && ok;
    }

    // Authenticated request (open mode) must still pass using auth_bucket
    {
        httplib::Request  req = make_req("GET", "/api/health");
        httplib::Response res;
        const HR result = call_guard(rl, "", "", true, req, res);
        ok = expect_true(result == HR::Unhandled,
                         "authenticated (open-mode) request must pass despite lockout") && ok;
    }

    return ok;
}
} // namespace

int main()
{
    bool all_ok = true;

    all_ok = unauth_bucket_exhausts_after_max_tokens() && all_ok;
    all_ok = auth_bucket_exhausts_after_max_tokens()   && all_ok;
    all_ok = non_api_route_bypasses_rate_limit()       && all_ok;
    all_ok = open_mode_allows_without_key()            && all_ok;
    all_ok = protected_mode_no_key_returns_401()       && all_ok;
    all_ok = correct_admin_key_passes()                && all_ok;
    all_ok = wrong_admin_key_returns_401()             && all_ok;
    all_ok = dual_headers_returns_400()                && all_ok;
    all_ok = restricted_key_on_allowed_route_passes()  && all_ok;
    all_ok = restricted_key_on_disallowed_route_returns_403() && all_ok;
    all_ok = rate_limit_returns_429()                  && all_ok;
    all_ok = non_api_route_bypasses_auth()             && all_ok;
    all_ok = lockout_blocks_unauth_but_not_auth()      && all_ok;

    return finish_test("api_guard_test", all_ok);
}
