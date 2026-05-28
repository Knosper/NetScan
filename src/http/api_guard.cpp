#include "http/api_guard.hpp"
#include "http/http_status.hpp"
#include "http/responses.hpp"
#include "http/route_utils.hpp"
#include "util/secret_utils.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>

static bool has_prefix(const std::string& s, const char* prefix)
{
    const std::size_t n = std::char_traits<char>::length(prefix);
    return s.size() > n && s.compare(0, n, prefix) == 0;
}

static bool has_suffix(const std::string& s, const char* suffix)
{
    const std::size_t n = std::char_traits<char>::length(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

static bool is_api_route(const std::string& path)
{
    return path == "/api" || has_prefix(path, "/api/");
}

namespace
{
static const double AUTH_RATE_REFILL    = 5.0;
static const double AUTH_RATE_MAX       = 20.0;
static const double UNAUTH_RATE_REFILL  = 1.0;
static const double UNAUTH_RATE_MAX     = 5.0;

static const int    LOCKOUT_FAIL_THRESHOLD  = 5;
static const double LOCKOUT_WINDOW_SECONDS  = 60.0;
static const double LOCKOUT_DURATION_SECONDS = 300.0;

struct TokenBucket
{
    double                                tokens;
    double                                max_tokens;
    double                                refill_per_second;
    std::chrono::steady_clock::time_point last_refill =
        std::chrono::steady_clock::now();

    explicit TokenBucket(double max, double refill)
        : tokens(max), max_tokens(max), refill_per_second(refill)
    {}
};

static void refill_bucket(TokenBucket& b, std::chrono::steady_clock::time_point now)
{
    const std::chrono::duration<double> elapsed = now - b.last_refill;
    b.tokens = std::min(b.max_tokens, b.tokens + elapsed.count() * b.refill_per_second);
    b.last_refill = now;
}

struct LockoutState
{
    std::vector<std::chrono::steady_clock::time_point> failure_times;
    std::chrono::steady_clock::time_point              locked_until =
        std::chrono::steady_clock::time_point{};
};

static bool is_in_lockout(LockoutState& ls, std::chrono::steady_clock::time_point now)
{
    return now < ls.locked_until;
}

static void record_auth_failure(LockoutState& ls, std::chrono::steady_clock::time_point now)
{
    const auto cutoff = now - std::chrono::duration<double>(LOCKOUT_WINDOW_SECONDS);
    ls.failure_times.erase(
        std::remove_if(ls.failure_times.begin(), ls.failure_times.end(),
                       [&cutoff](const std::chrono::steady_clock::time_point& t)
                       { return t < cutoff; }),
        ls.failure_times.end());

    ls.failure_times.push_back(now);
    if (static_cast<int>(ls.failure_times.size()) >= LOCKOUT_FAIL_THRESHOLD)
        ls.locked_until = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                    std::chrono::duration<double>(LOCKOUT_DURATION_SECONDS));
}

struct RateLimiter
{
    std::mutex   mutex;
    TokenBucket  auth_bucket{AUTH_RATE_MAX, AUTH_RATE_REFILL};
    TokenBucket  unauth_bucket{UNAUTH_RATE_MAX, UNAUTH_RATE_REFILL};
    LockoutState lockout;

    bool allow_authenticated(std::chrono::steady_clock::time_point now);
    bool allow_unauthenticated(std::chrono::steady_clock::time_point now);
    void record_failure(std::chrono::steady_clock::time_point now);
};

bool RateLimiter::allow_authenticated(std::chrono::steady_clock::time_point now)
{
    std::lock_guard<std::mutex> lock(mutex);
    refill_bucket(auth_bucket, now);
    if (auth_bucket.tokens < 1.0)
        return false;
    auth_bucket.tokens -= 1.0;
    return true;
}

bool RateLimiter::allow_unauthenticated(std::chrono::steady_clock::time_point now)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (is_in_lockout(lockout, now))
        return false;
    refill_bucket(unauth_bucket, now);
    if (unauth_bucket.tokens < 1.0)
        return false;
    unauth_bucket.tokens -= 1.0;
    return true;
}

void RateLimiter::record_failure(std::chrono::steady_clock::time_point now)
{
    std::lock_guard<std::mutex> lock(mutex);
    record_auth_failure(lockout, now);
}
} // namespace

static bool is_allowed_user_get(const std::string& path)
{
    if (path == "/api/health"         || path == "/api/dashboard" ||
        path == "/api/hosts"          || path == "/api/scan/status" ||
        path == "/api/scans"          || path == "/api/profiles" ||
        path == "/api/settings"       || path == "/api/scheduler/jobs")
        return true;
    return has_prefix(path, "/api/scans/") || has_prefix(path, "/api/hosts/");
}

static bool is_allowed_user_post(const std::string& path)
{
    // /api/scan/start: allowed here; target allowlist check happens in the handler.
    if (path == "/api/scan/start")
        return true;
    // Prefix /api/scan/ and suffix /abort must not overlap; at least one character
    // of scan ID must appear between them. Minimum: /api/scan/<id>/abort
    static const char SCAN_PREFIX[] = "/api/scan/";
    static const char ABORT_SUFFIX[] = "/abort";
    static const std::size_t MIN_LEN =
        (sizeof(SCAN_PREFIX) - 1) + (sizeof(ABORT_SUFFIX) - 1) + 1;
    return path.size() >= MIN_LEN &&
           has_prefix(path, SCAN_PREFIX) && has_suffix(path, ABORT_SUFFIX);
}

static bool is_allowed_user_route(const httplib::Request& req)
{
    if (req.method == "GET")
        return is_allowed_user_get(req.path);
    if (req.method == "POST")
        return is_allowed_user_post(req.path);
    return false;
}

static bool is_valid_admin_key(const httplib::Request& req, const std::string& admin_hash)
{
    if (admin_hash.empty() || !is_api_route(req.path))
        return false;
    return req.has_header("X-API-Key") &&
           verify_secret_key(req.get_header_value("X-API-Key"), admin_hash);
}

static bool is_valid_restricted_key(const httplib::Request& req,
                                    const std::string& restricted_hash)
{
    if (restricted_hash.empty())
        return false;
    return req.has_header("X-NetScan-Restricted-Key") &&
           verify_secret_key(req.get_header_value("X-NetScan-Restricted-Key"), restricted_hash);
}

struct AuthEvaluation
{
    bool has_api_key = false;
    bool has_restricted_key = false;
    bool admin_valid = false;
    bool restricted_valid = false;
    bool authenticated = false;
};

static AuthEvaluation evaluate_auth_request(const httplib::Request& req,
                                            const std::string& admin_hash,
                                            const std::string& restricted_hash, bool open_mode)
{
    AuthEvaluation auth;
    auth.has_api_key = req.has_header("X-API-Key");
    auth.has_restricted_key = req.has_header("X-NetScan-Restricted-Key");

    if (open_mode)
    {
        auth.authenticated = true;
        return auth;
    }

    if (auth.has_api_key && auth.has_restricted_key)
        return auth;

    if (auth.has_restricted_key)
    {
        auth.restricted_valid = is_valid_restricted_key(req, restricted_hash);
        auth.authenticated = auth.restricted_valid;
        return auth;
    }

    if (auth.has_api_key)
    {
        auth.admin_valid = is_valid_admin_key(req, admin_hash);
        auth.authenticated = auth.admin_valid;
    }

    return auth;
}

// Straight-line auth guard with early returns.
static httplib::Server::HandlerResponse apply_auth_guard(
    RateLimiter& rate_limiter, const std::string& admin_hash,
    const std::string& restricted_hash, bool open_mode,
    const httplib::Request& req, httplib::Response& res)
{
    using HR = httplib::Server::HandlerResponse;

    if (!is_api_route(req.path))
        return HR::Unhandled;

    const auto now = std::chrono::steady_clock::now();
    const AuthEvaluation auth =
        evaluate_auth_request(req, admin_hash, restricted_hash, open_mode);

    // 1. Rate limit — bucket selected by auth status
    const bool allowed = auth.authenticated ? rate_limiter.allow_authenticated(now)
                                            : rate_limiter.allow_unauthenticated(now);
    if (!allowed)
    {
        set_json_error(res, JsonError{http_status::TOO_MANY_REQUESTS, "too_many_requests",
                                      "rate limit exceeded"});
        return HR::Handled;
    }

    // 2. Both auth headers simultaneously → 400
    if (auth.has_api_key && auth.has_restricted_key)
    {
        set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request",
                                      "only one auth header may be set"});
        return HR::Handled;
    }

    // 3. Restricted key present → verify + allowlist
    if (auth.has_restricted_key)
    {
        if (!auth.restricted_valid)
        {
            rate_limiter.record_failure(now);
            set_json_error(res,
                           JsonError{http_status::UNAUTHORIZED, "unauthorized", "unauthorized"});
            return HR::Handled;
        }
        if (!is_allowed_user_route(req))
        {
            set_json_error(res, JsonError{http_status::FORBIDDEN, "forbidden",
                                          "route not allowed for user key"});
            return HR::Handled;
        }
        return HR::Unhandled;
    }

    // 4. Admin key present → verify (only for API routes with a configured key)
    if (auth.has_api_key)
    {
        if (!auth.admin_valid)
        {
            rate_limiter.record_failure(now);
            set_json_error(res,
                           JsonError{http_status::UNAUTHORIZED, "unauthorized", "unauthorized"});
            return HR::Handled;
        }
        return HR::Unhandled;
    }

    // 5. Open mode → allow through
    if (open_mode)
        return HR::Unhandled;

    // 6. No credentials → 401
    rate_limiter.record_failure(now);
    set_json_error(res, JsonError{http_status::UNAUTHORIZED, "unauthorized", "unauthorized"});
    return HR::Handled;
}

void register_api_response_headers(httplib::Server& svr, const AppConfig& config)
{
    const bool tls_enabled = config.tls_enabled;
    static const char* CSP_POLICY =
        "default-src 'self'; "
        "base-uri 'self'; "
        "frame-ancestors 'none'; "
        "object-src 'none'; "
        "script-src 'self'; "
        "style-src 'self'; "
        "img-src 'self' data:; "
        "font-src 'self'; "
        "connect-src 'self'";

    svr.set_post_routing_handler(
        [tls_enabled](const httplib::Request&, httplib::Response& res)
        {
            res.set_header("X-Content-Type-Options", "nosniff");
            res.set_header("X-Frame-Options", "DENY");
            res.set_header("Cache-Control", "no-store");
            res.set_header("Content-Security-Policy", CSP_POLICY);
            if (tls_enabled)
                res.set_header("Strict-Transport-Security", "max-age=31536000");
        });
}

void register_api_key_guard(httplib::Server& svr, const AppConfig& config,
                            const AuthConfig& auth_config)
{
    const std::string admin_hash = auth_config.admin_api_key_hash;
    const std::string restricted_hash = auth_config.restricted_api_key_hash;
    const bool open_mode = is_open_mode(config.host, auth_config);
    std::shared_ptr<RateLimiter> rate_limiter = std::make_shared<RateLimiter>();
    svr.set_pre_routing_handler(
        [admin_hash, restricted_hash, open_mode, rate_limiter](const httplib::Request& req,
                                                               httplib::Response& res)
        {
            return apply_auth_guard(*rate_limiter, admin_hash, restricted_hash, open_mode,
                                    req, res);
        });
}

void MethodRegistry::add(const std::string& pattern, const std::string& method)
{
    entries.push_back({std::regex(pattern), method});
}

std::string MethodRegistry::find_allowed(const std::string& path) const
{
    std::vector<std::string> methods;
    for (const Entry& e : entries)
    {
        if (std::regex_match(path, e.pattern) &&
            std::find(methods.begin(), methods.end(), e.method) == methods.end())
        {
            methods.push_back(e.method);
        }
    }
    if (methods.empty())
        return {};

    std::string result;
    for (const std::string& m : methods)
    {
        if (!result.empty())
            result += ", ";
        result += m;
    }
    return result;
}

void register_error_handlers(httplib::Server& svr, const MethodRegistry& registry)
{
    svr.set_error_handler(
        [registry](const httplib::Request& req, httplib::Response& res)
        {
            if (res.status != http_status::NOT_FOUND || !res.body.empty())
                return;
            if (!is_api_route(req.path))
                return;

            const std::string allowed = registry.find_allowed(req.path);
            if (!allowed.empty())
            {
                res.set_header("Allow", allowed);
                set_json_error(res, JsonError{http_status::METHOD_NOT_ALLOWED, "method_not_allowed",
                                              "Method not allowed for this endpoint"});
                return;
            }
            set_json_error(res, JsonError{http_status::NOT_FOUND, "not_found",
                                          "API endpoint not found"});
        });
}
