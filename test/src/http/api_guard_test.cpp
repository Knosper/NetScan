#include "test_output.hpp"
#include "util/secret_utils.hpp"
#include "http/http_status.hpp"

#include <chrono>
#include <string>
#include <thread>

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
    const auto now = std::chrono::steady_clock::now();
    limiter.client("10.0.0.1", now).unauth_bucket.tokens = 1.0;

    ok = expect_true(limiter.allow_unauthenticated("10.0.0.1", now),
                     "last token: unauthenticated request should be allowed") && ok;
    ok = expect_true(!limiter.allow_unauthenticated("10.0.0.1", now),
                     "empty bucket: unauthenticated request should be rate-limited") && ok;

    return ok;
}

bool auth_bucket_exhausts_after_max_tokens()
{
    bool ok = true;

    RateLimiter limiter;
    const auto now = std::chrono::steady_clock::now();
    limiter.client("10.0.0.1", now).auth_bucket.tokens = 1.0;

    ok = expect_true(limiter.allow_authenticated("10.0.0.1", now),
                     "last token: authenticated request should be allowed") && ok;
    ok = expect_true(!limiter.allow_authenticated("10.0.0.1", now),
                     "empty auth bucket: authenticated request should be rate-limited") && ok;

    return ok;
}

bool non_api_route_bypasses_rate_limit()
{
    RateLimiter limiter;
    limiter.client("", std::chrono::steady_clock::now()).unauth_bucket.tokens = 0.0;

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
    rl.client("", std::chrono::steady_clock::now()).unauth_bucket.tokens = 0.0;

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

httplib::Request make_client_req(const std::string& remote_addr)
{
    httplib::Request req = make_req("GET", "/api/health");
    req.remote_addr = remote_addr;
    return req;
}

// A client with a wrong key locks out only itself; other addresses keep
// their own buckets and lockout state.
bool lockout_is_per_client()
{
    bool ok = true;

    const std::string real_hash = hash_secret_key("realkey");
    RateLimiter rl;

    for (int i = 0; i < 5; ++i)
    {
        httplib::Request  req = make_client_req("192.168.1.10");
        req.set_header("X-API-Key", "wrongkey");
        httplib::Response res;
        call_guard(rl, real_hash, "", false, req, res);
    }

    {
        httplib::Request  req = make_client_req("192.168.1.10");
        httplib::Response res;
        const HR result = call_guard(rl, real_hash, "", false, req, res);
        ok = expect_true(result == HR::Handled && res.status == http_status::TOO_MANY_REQUESTS,
                         "locked-out client should get 429") && ok;
    }

    {
        httplib::Request  req = make_client_req("192.168.1.20");
        httplib::Response res;
        const HR result = call_guard(rl, real_hash, "", false, req, res);
        ok = expect_true(result == HR::Handled && res.status == http_status::UNAUTHORIZED,
                         "other client should not inherit the lockout") && ok;
    }

    rl.client("192.168.1.30", std::chrono::steady_clock::now()).auth_bucket.tokens = 0.0;
    {
        httplib::Request  req = make_client_req("192.168.1.40");
        req.set_header("X-API-Key", "realkey");
        httplib::Response res;
        const HR result = call_guard(rl, real_hash, "", false, req, res);
        ok = expect_true(result == HR::Unhandled,
                         "exhausted bucket of one client should not limit another") && ok;
    }

    return ok;
}

// The client table stays bounded: idle entries are dropped once it is full,
// and if every entry is active new clients share one overflow entry.
bool client_table_is_bounded()
{
    bool ok = true;

    RateLimiter rl;
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < MAX_TRACKED_CLIENTS; ++i)
        rl.client("10.1." + std::to_string(i), start);

    rl.client("10.2.0.1", start);
    ok = expect_true(rl.clients.size() == MAX_TRACKED_CLIENTS + 1 &&
                     rl.clients.count(OVERFLOW_CLIENT_KEY) == 1 &&
                     rl.clients.count("10.2.0.1") == 0,
                     "active full table should route new clients to the overflow entry") && ok;

    rl.client("10.1.0", start).lockout.locked_until = start + std::chrono::hours(1);
    const auto later = start + std::chrono::minutes(10);
    rl.client("10.2.0.2", later);
    ok = expect_true(rl.clients.size() == 2 && rl.clients.count("10.2.0.2") == 1 &&
                     rl.clients.count("10.1.0") == 1,
                     "idle clients should be evicted, locked-out clients kept") && ok;

    return ok;
}

httplib::Request make_browser_req(const std::string& method, const std::string& path,
                                  const std::string& host = "127.0.0.1:8080")
{
    httplib::Request req = make_req(method, path);
    req.set_header("Host", host);
    return req;
}

bool expect_rejected(httplib::Request& req, int status, const std::string& message,
                     bool enforce_loopback_host = true,
                     const std::vector<std::string>& allowed_hosts = {})
{
    httplib::Response res;
    const HR result = apply_request_origin_guard(enforce_loopback_host, allowed_hosts, req, res);
    bool ok = expect_true(result == HR::Handled, message + ": should be handled");
    ok = expect_true(res.status == status,
                     message + ": expected " + std::to_string(status) + ", got " +
                         std::to_string(res.status)) && ok;
    return ok;
}

bool expect_passed(httplib::Request& req, const std::string& message,
                   bool enforce_loopback_host = true,
                   const std::vector<std::string>& allowed_hosts = {})
{
    httplib::Response res;
    return expect_true(apply_request_origin_guard(enforce_loopback_host, allowed_hosts, req, res) ==
                           HR::Unhandled,
                       message + ": should pass the origin guard");
}

bool text_plain_form_post_is_rejected()
{
    // <form enctype="text/plain"> smuggling a JSON body (no CORS preflight).
    httplib::Request req = make_browser_req("POST", "/api/scheduler/jobs");
    req.set_header("Content-Type", "text/plain");
    req.body = R"({"target":"10.0.0.0/24","x":"="})";
    return expect_rejected(req, http_status::UNSUPPORTED_MEDIA_TYPE, "text/plain POST");
}

bool urlencoded_bodyless_post_is_rejected()
{
    httplib::Request req = make_browser_req("POST", "/api/shutdown");
    req.set_header("Content-Type", "application/x-www-form-urlencoded");
    return expect_rejected(req, http_status::UNSUPPORTED_MEDIA_TYPE,
                           "urlencoded empty POST /api/shutdown");
}

bool body_without_content_type_is_rejected()
{
    httplib::Request req = make_browser_req("PUT", "/api/profiles/1");
    req.body = "{}";
    return expect_rejected(req, http_status::UNSUPPORTED_MEDIA_TYPE,
                           "PUT with body but no Content-Type");
}

bool foreign_origin_is_rejected()
{
    bool ok = true;

    const char* const origins[] = {"https://evil.example", "null", "http://127.0.0.1:3000",
                                   "http://127.0.0.1:8080.evil.example"};
    for (const char* origin : origins)
    {
        httplib::Request req = make_browser_req("POST", "/api/setup/reset");
        req.set_header("Origin", origin);
        req.set_header("Content-Type", "application/json");
        req.body = "{}";
        ok = expect_rejected(req, http_status::FORBIDDEN, std::string("Origin ") + origin) && ok;
    }

    return ok;
}

bool cross_site_fetch_metadata_is_rejected()
{
    bool ok = true;

    httplib::Request cross = make_browser_req("POST", "/api/profiles/1/run");
    cross.set_header("Sec-Fetch-Site", "cross-site");
    ok = expect_rejected(cross, http_status::FORBIDDEN, "Sec-Fetch-Site: cross-site") && ok;

    httplib::Request same_site = make_browser_req("POST", "/api/profiles/1/run");
    same_site.set_header("Sec-Fetch-Site", "same-site");
    ok = expect_rejected(same_site, http_status::FORBIDDEN, "Sec-Fetch-Site: same-site") && ok;

    return ok;
}

bool same_origin_ui_requests_pass()
{
    bool ok = true;

    httplib::Request json_post = make_browser_req("POST", "/api/scan/start");
    json_post.set_header("Origin", "http://127.0.0.1:8080");
    json_post.set_header("Sec-Fetch-Site", "same-origin");
    json_post.set_header("Content-Type", "application/json; charset=utf-8");
    json_post.body = R"({"target":"127.0.0.1"})";
    ok = expect_passed(json_post, "same-origin JSON POST") && ok;

    httplib::Request bodyless_delete = make_browser_req("DELETE", "/api/scans/1",
                                                        "localhost:8080");
    bodyless_delete.set_header("Origin", "http://LOCALHOST:8080");
    bodyless_delete.set_header("Sec-Fetch-Site", "same-origin");
    ok = expect_passed(bodyless_delete, "same-origin bodyless DELETE") && ok;

    httplib::Request cli_post = make_browser_req("POST", "/api/scan/1/abort");
    ok = expect_passed(cli_post, "CLI POST without Origin and body") && ok;

    httplib::Request cross_get = make_browser_req("GET", "/api/scans");
    cross_get.set_header("Origin", "https://evil.example");
    cross_get.set_header("Sec-Fetch-Site", "cross-site");
    ok = expect_passed(cross_get, "cross-origin GET (response unreadable by SOP)") && ok;

    return ok;
}

bool host_allowlist_blocks_dns_rebinding()
{
    bool ok = true;

    // 0.0.0.0 reaches loopback on Linux/macOS ("0.0.0.0 day"); IPv4-mapped and
    // trailing-dot loopback spellings are never produced by browsers and fail closed.
    const char* const rejected[] = {"rebind.evil.example:8080", "127.0.0.1.evil.example",
                                    "[::1]evil.example", "0.0.0.0:8080",
                                    "[::ffff:127.0.0.1]:8080", "localhost.:8080"};
    for (const char* host : rejected)
    {
        httplib::Request req = make_browser_req("GET", "/api/scans", host);
        ok = expect_rejected(req, http_status::FORBIDDEN, std::string("foreign Host ") + host) && ok;
    }

    httplib::Request rebound_static = make_browser_req("GET", "/", "evil.example");
    ok = expect_rejected(rebound_static, http_status::FORBIDDEN, "rebound Host on /") && ok;

    httplib::Request missing = make_req("GET", "/api/scans");
    ok = expect_rejected(missing, http_status::FORBIDDEN, "missing Host") && ok;

    const char* const allowed[] = {"127.0.0.1:8080", "127.0.0.1", "localhost:9000",
                                   "LocalHost", "[::1]:8080", "127.0.0.2:8080"};
    for (const char* host : allowed)
    {
        httplib::Request req = make_browser_req("GET", "/api/scans", host);
        ok = expect_passed(req, std::string("loopback Host ") + host) && ok;
    }

    return ok;
}

// Reverse proxy on the same machine: the proxy forwards its public name as Host.
// Only the configured name passes; rebinding names and Origin checks stay in force.
bool allowed_hosts_extend_loopback_allowlist()
{
    bool ok = true;
    const std::vector<std::string> allowed = parse_allowed_hosts(" NetScan.Example , nas.lan");

    httplib::Request proxied = make_browser_req("GET", "/api/scans", "netscan.example");
    ok = expect_passed(proxied, "configured proxy Host", true, allowed) && ok;

    httplib::Request proxied_port = make_browser_req("GET", "/", "NAS.lan:8443");
    ok = expect_passed(proxied_port, "configured Host with port", true, allowed) && ok;

    httplib::Request proxied_post = make_browser_req("POST", "/api/scan/start", "netscan.example");
    proxied_post.set_header("Origin", "https://netscan.example");
    proxied_post.set_header("Sec-Fetch-Site", "same-origin");
    proxied_post.set_header("Content-Type", "application/json");
    proxied_post.body = "{}";
    ok = expect_passed(proxied_post, "same-origin POST through proxy", true, allowed) && ok;

    httplib::Request other = make_browser_req("GET", "/api/scans", "rebind.evil.example");
    ok = expect_rejected(other, http_status::FORBIDDEN, "unlisted Host with allowed_hosts", true,
                         allowed) && ok;

    httplib::Request suffix = make_browser_req("GET", "/api/scans", "netscan.example.evil.example");
    ok = expect_rejected(suffix, http_status::FORBIDDEN, "allowed name as prefix", true,
                         allowed) && ok;

    httplib::Request foreign_origin = make_browser_req("POST", "/api/scan/start", "netscan.example");
    foreign_origin.set_header("Origin", "https://evil.example");
    foreign_origin.set_header("Content-Type", "application/json");
    foreign_origin.body = "{}";
    ok = expect_rejected(foreign_origin, http_status::FORBIDDEN, "foreign Origin through proxy",
                         true, allowed) && ok;

    return ok;
}

bool host_allowlist_disabled_for_non_loopback_bind()
{
    httplib::Request req = make_browser_req("GET", "/api/scans", "netscan.lan:8443");
    return expect_passed(req, "LAN Host with non-loopback bind", false);
}

// End-to-end: a rebound page on the setup server must not read the generated key.
bool setup_server_guard_blocks_rebinding_end_to_end()
{
    httplib::Server svr;
    register_setup_request_guard(svr);
    svr.Post("/api/setup", [](const httplib::Request&, httplib::Response& res)
             { res.set_content(R"({"adminKey":"secret-admin-key"})", "application/json"); });
    svr.Get("/api/health", [](const httplib::Request&, httplib::Response& res)
            { res.set_content(R"({"mode":"setup"})", "application/json"); });

    const int port = svr.bind_to_any_port("127.0.0.1");
    if (port <= 0)
        return expect_true(false, "setup guard test: bind failed");
    std::thread server_thread([&svr]() { svr.listen_after_bind(); });
    svr.wait_until_ready();

    bool ok = true;
    httplib::Client client("127.0.0.1", port);
    const std::string body = R"({"host":"127.0.0.1","port":8080})";

    httplib::Result rebound = client.Post(
        "/api/setup", httplib::Headers{{"Host", "rebind.evil.example:" + std::to_string(port)}},
        body, "application/json");
    ok = expect_true(rebound && rebound->status == http_status::FORBIDDEN,
                     "setup: rebound Host POST /api/setup should be 403") && ok;
    ok = expect_true(rebound && rebound->body.find("secret") == std::string::npos,
                     "setup: rebound Host must not see the admin key") && ok;

    httplib::Result rebound_get = client.Get(
        "/api/health", httplib::Headers{{"Host", "rebind.evil.example"}});
    ok = expect_true(rebound_get && rebound_get->status == http_status::FORBIDDEN,
                     "setup: rebound Host GET should be 403") && ok;

    httplib::Result legit = client.Post("/api/setup", body, "application/json");
    ok = expect_true(legit && legit->status == http_status::OK,
                     "setup: loopback POST /api/setup should pass") && ok;
    ok = expect_true(legit && legit->get_header_value("Cache-Control") == "no-store",
                     "setup: key response must not be cached") && ok;
    ok = expect_true(legit && legit->get_header_value("X-Frame-Options") == "DENY",
                     "setup: wizard must not be frameable") && ok;

    svr.stop();
    server_thread.join();
    return ok;
}

bool hostname_from_authority_parses_variants()
{
    bool ok = true;
    ok = expect_true(hostname_from_authority("127.0.0.1:8080") == "127.0.0.1", "ipv4:port") && ok;
    ok = expect_true(hostname_from_authority("localhost") == "localhost", "bare name") && ok;
    ok = expect_true(hostname_from_authority("[::1]:8080") == "::1", "[ipv6]:port") && ok;
    ok = expect_true(hostname_from_authority("[::1]") == "::1", "[ipv6]") && ok;
    ok = expect_true(hostname_from_authority("[::1]x").empty(), "junk after ]") && ok;
    ok = expect_true(hostname_from_authority("::1").empty(), "unbracketed ipv6") && ok;
    ok = expect_true(hostname_from_authority("").empty(), "empty") && ok;
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
    all_ok = lockout_is_per_client()                   && all_ok;
    all_ok = client_table_is_bounded()                 && all_ok;
    all_ok = text_plain_form_post_is_rejected()        && all_ok;
    all_ok = urlencoded_bodyless_post_is_rejected()    && all_ok;
    all_ok = body_without_content_type_is_rejected()   && all_ok;
    all_ok = foreign_origin_is_rejected()              && all_ok;
    all_ok = cross_site_fetch_metadata_is_rejected()   && all_ok;
    all_ok = same_origin_ui_requests_pass()            && all_ok;
    all_ok = host_allowlist_blocks_dns_rebinding()     && all_ok;
    all_ok = host_allowlist_disabled_for_non_loopback_bind() && all_ok;
    all_ok = allowed_hosts_extend_loopback_allowlist() && all_ok;
    all_ok = hostname_from_authority_parses_variants() && all_ok;
    all_ok = setup_server_guard_blocks_rebinding_end_to_end() && all_ok;

    return finish_test("api_guard_test", all_ok);
}
