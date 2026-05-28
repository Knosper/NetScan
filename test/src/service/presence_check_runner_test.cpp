#include "service/presence_check_runner.hpp"
#include "service/presence_service.hpp"
#include "test_output.hpp"

#include "httplib/httplib.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

namespace
{
bool expect(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << "\n";
        return false;
    }
    return true;
}

int reserve_unused_loopback_port()
{
    int port = -1;
    {
        httplib::Server server;
        port = server.bind_to_any_port("127.0.0.1");
        if (port > 0)
            server.stop();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    return port;
}

PresenceTracker make_ping_tracker(const std::string& target, int timeout_ms = 3000)
{
    PresenceTracker tracker;
    tracker.id = 1;
    tracker.target = target;
    tracker.check_type = "ping";
    tracker.timeout_ms = timeout_ms;
    return tracker;
}

PresenceTracker make_http_tracker(const std::string& target, const std::string& url = "",
                                  int port = 0)
{
    PresenceTracker tracker;
    tracker.id = 1;
    tracker.target = target;
    tracker.check_type = "http";
    tracker.url = url;
    tracker.port = port;
    tracker.timeout_ms = 3000;
    return tracker;
}

bool test_http_url_default_path()
{
    PresenceTracker tracker = make_http_tracker("192.168.1.1");
    HttpTarget target;
    std::string error;
    if (!expect(parse_http_url(tracker, target, error), "default path: parse must succeed"))
        return false;
    bool ok = true;
    ok = expect(target.host == "192.168.1.1", "default path: host must be target") && ok;
    ok = expect(target.path == "/", "default path: path must be /") && ok;
    ok = expect(target.port == 80, "default path: port must default to 80") && ok;
    return ok;
}

bool test_http_url_explicit_path()
{
    PresenceTracker tracker = make_http_tracker("", "http://example.com/api/status");
    HttpTarget target;
    std::string error;
    if (!expect(parse_http_url(tracker, target, error), "explicit path: parse must succeed"))
        return false;
    bool ok = true;
    ok = expect(target.host == "example.com", "explicit path: host") && ok;
    ok = expect(target.path == "/api/status", "explicit path: path") && ok;
    ok = expect(target.port == 80, "explicit path: default port") && ok;
    return ok;
}

bool test_http_url_explicit_port_in_url()
{
    PresenceTracker tracker = make_http_tracker("", "http://192.168.0.1:8080/health");
    HttpTarget target;
    std::string error;
    if (!expect(parse_http_url(tracker, target, error), "explicit port in URL: parse must succeed"))
        return false;
    bool ok = true;
    ok = expect(target.host == "192.168.0.1", "explicit port in URL: host") && ok;
    ok = expect(target.port == 8080, "explicit port in URL: port") && ok;
    ok = expect(target.path == "/health", "explicit port in URL: path") && ok;
    return ok;
}

bool test_http_url_port_from_tracker()
{
    PresenceTracker tracker = make_http_tracker("10.0.0.1", "", 9000);
    HttpTarget target;
    std::string error;
    if (!expect(parse_http_url(tracker, target, error), "tracker port: parse must succeed"))
        return false;
    bool ok = true;
    ok = expect(target.host == "10.0.0.1", "tracker port: host") && ok;
    ok = expect(target.port == 9000, "tracker port: port from tracker") && ok;
    return ok;
}

bool test_http_url_root_path_when_no_slash()
{
    PresenceTracker tracker = make_http_tracker("", "http://myhost");
    HttpTarget target;
    std::string error;
    if (!expect(parse_http_url(tracker, target, error), "no-slash URL: parse must succeed"))
        return false;
    return expect(target.path == "/", "no-slash URL: path defaults to /");
}

bool test_http_url_rejects_https()
{
    PresenceTracker tracker = make_http_tracker("", "https://example.com/");
    HttpTarget target;
    std::string error;
    const bool ok = parse_http_url(tracker, target, error);
    return expect(!ok, "https URL must be rejected") &&
           expect(!error.empty(), "https URL: error message must be set");
}

bool test_http_url_rejects_empty_host()
{
    PresenceTracker tracker = make_http_tracker("", "http:///path");
    HttpTarget target;
    std::string error;
    const bool ok = parse_http_url(tracker, target, error);
    return expect(!ok, "empty host URL must be rejected") &&
           expect(!error.empty(), "empty host URL: error message must be set");
}

bool test_http_url_rejects_invalid_port()
{
    PresenceTracker tracker = make_http_tracker("", "http://host:99999/");
    HttpTarget target;
    std::string error;
    const bool ok = parse_http_url(tracker, target, error);
    return expect(!ok, "out-of-range port must be rejected") &&
           expect(!error.empty(), "out-of-range port: error message must be set");
}

bool test_http_url_rejects_zero_port()
{
    PresenceTracker tracker = make_http_tracker("", "http://host:0/");
    HttpTarget target;
    std::string error;
    const bool ok = parse_http_url(tracker, target, error);
    return expect(!ok, "port 0 must be rejected") &&
           expect(!error.empty(), "port 0: error message must be set");
}

bool test_ping_empty_target_returns_error()
{
    DefaultPresenceCheckRunner runner;
    const PresenceCheckResult result = runner.run(make_ping_tracker(""));
    return expect(result.status == "error", "empty ping target must return error") &&
           expect(!result.error.empty(), "empty ping target: error message must be set");
}

bool test_ping_shell_injection_target_returns_error()
{
    DefaultPresenceCheckRunner runner;
    // Characters like ';', '&', '|', '$', '`', space are not in the safe set
    const PresenceCheckResult result = runner.run(make_ping_tracker("host; rm -rf /"));
    return expect(result.status == "error", "shell-unsafe ping target must return error") &&
           expect(!result.error.empty(), "shell-unsafe ping target: error message must be set");
}

bool test_ping_slash_in_target_returns_error()
{
    DefaultPresenceCheckRunner runner;
    // '/' is not in the allowed character set for ping targets
    const PresenceCheckResult result = runner.run(make_ping_tracker("192.168.1.0/24"));
    return expect(result.status == "error", "slash in ping target must return error") &&
           expect(!result.error.empty(), "slash in ping target: error message must be set");
}

bool test_ping_space_in_target_returns_error()
{
    DefaultPresenceCheckRunner runner;
    const PresenceCheckResult result = runner.run(make_ping_tracker("host name"));
    return expect(result.status == "error", "space in ping target must return error") &&
           expect(!result.error.empty(), "space in ping target: error message must be set");
}

bool test_unsupported_check_type_returns_error()
{
    DefaultPresenceCheckRunner runner;
    PresenceTracker tracker = make_ping_tracker("192.168.1.1");
    tracker.check_type = "udp";
    const PresenceCheckResult result = runner.run(tracker);
    return expect(result.status == "error", "unsupported check type must return error") &&
           expect(!result.error.empty(), "unsupported check type: error message must be set");
}

// Windows-specific: a target beginning with '-' must be rejected to prevent
// it from being interpreted as a flag by the ping utility.
bool test_ping_leading_dash_target()
{
    DefaultPresenceCheckRunner runner;
    const PresenceCheckResult result = runner.run(make_ping_tracker("-n 1 127.0.0.1"));
#ifdef _WIN32
    return expect(result.status == "error",
                  "Windows: leading-dash ping target must return error");
#else
    // On POSIX the leading-dash character check is not present; the space in
    // the target still makes it invalid through the general character filter.
    return expect(result.status == "error",
                  "POSIX: space-containing ping target must return error");
#endif
}

bool test_ping_pure_dash_target_windows()
{
#ifndef _WIN32
    return true; // Windows-only test; no-op on POSIX
#else
    DefaultPresenceCheckRunner runner;
    const PresenceCheckResult result = runner.run(make_ping_tracker("-"));
    return expect(result.status == "error",
                  "Windows: dash-only ping target must return error");
#endif
}

struct ScopedTestServer
{
    httplib::Server svr;
    std::thread thread;
    int port = -1;

    void start()
    {
        port = svr.bind_to_any_port("127.0.0.1");
        thread = std::thread([this]() { svr.listen_after_bind(); });
        // Poll until the server is actually accepting connections.
        for (int i = 0; i < 100 && !svr.is_running(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    ~ScopedTestServer()
    {
        svr.stop();
        if (thread.joinable())
            thread.join();
    }
};

bool test_http_check_2xx_returns_up()
{
    ScopedTestServer s;
    s.svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("ok", "text/plain");
    });
    s.start();
    if (!expect(s.port > 0, "2xx test: server must bind"))
        return false;

    const std::string url =
        "http://127.0.0.1:" + std::to_string(s.port) + "/health";
    PresenceTracker tracker = make_http_tracker("", url);
    DefaultPresenceCheckRunner runner;
    const PresenceCheckResult result = runner.run(tracker);

    bool ok = true;
    ok = expect(result.status == "up", "2xx: status must be up") && ok;
    ok = expect(result.error.empty(), "2xx: error must be empty") && ok;
    ok = expect(result.latency_ms >= 0, "2xx: latency_ms must be non-negative") && ok;
    return ok;
}

bool test_http_check_5xx_returns_down()
{
    ScopedTestServer s;
    s.svr.Get("/fail", [](const httplib::Request&, httplib::Response& res) {
        res.status = 503;
        res.set_content("service unavailable", "text/plain");
    });
    s.start();
    if (!expect(s.port > 0, "5xx test: server must bind"))
        return false;

    const std::string url =
        "http://127.0.0.1:" + std::to_string(s.port) + "/fail";
    PresenceTracker tracker = make_http_tracker("", url);
    DefaultPresenceCheckRunner runner;
    const PresenceCheckResult result = runner.run(tracker);

    bool ok = true;
    ok = expect(result.status == "down", "5xx: status must be down") && ok;
    ok = expect(result.error.find("503") != std::string::npos,
                "5xx: error must mention 503") && ok;
    return ok;
}

bool test_http_check_4xx_boundary_returns_up()
{
    ScopedTestServer s;
    s.svr.Get("/boundary", [](const httplib::Request&, httplib::Response& res) {
        res.status = 404;
        res.set_content("not found", "text/plain");
    });
    s.start();
    if (!expect(s.port > 0, "4xx boundary test: server must bind"))
        return false;

    const std::string url =
        "http://127.0.0.1:" + std::to_string(s.port) + "/boundary";
    PresenceTracker tracker = make_http_tracker("", url);
    DefaultPresenceCheckRunner runner;
    const PresenceCheckResult result = runner.run(tracker);

    // Range 100-499 is "up" — 404 must not be treated as down.
    return expect(result.status == "up", "4xx boundary: status 404 must be up");
}

bool test_http_check_unbound_local_port_returns_non_up()
{
    const int port = reserve_unused_loopback_port();
    if (!expect(port > 0, "connection refused test: must reserve a loopback port"))
        return false;

    PresenceTracker tracker =
        make_http_tracker("", "http://127.0.0.1:" + std::to_string(port) + "/probe");
    tracker.timeout_ms = 500;
    DefaultPresenceCheckRunner runner;
    const PresenceCheckResult result = runner.run(tracker);

    bool ok = true;
    ok = expect(result.status != "up",
                "unbound local port: status must not be up, got '" + result.status + "'") && ok;
    ok = expect(result.status != "error",
                "unbound local port: status must not be error, got error '" + result.error + "'") && ok;
    return ok;
}

bool test_http_check_timeout_returns_timeout()
{
    ScopedTestServer s;
    s.svr.Get("/hang", [](const httplib::Request&, httplib::Response& res) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        res.set_content("late", "text/plain");
    });
    s.start();
    if (!expect(s.port > 0, "timeout test: server must bind"))
        return false;

    const std::string url =
        "http://127.0.0.1:" + std::to_string(s.port) + "/hang";
    PresenceTracker tracker = make_http_tracker("", url);
    tracker.timeout_ms = 300;
    DefaultPresenceCheckRunner runner;
    const PresenceCheckResult result = runner.run(tracker);

    return expect(result.status == "timeout", "timeout: status must be timeout");
}
} // namespace

int main()
{
    bool all_ok = true;

    all_ok = test_http_url_default_path() && all_ok;
    all_ok = test_http_url_explicit_path() && all_ok;
    all_ok = test_http_url_explicit_port_in_url() && all_ok;
    all_ok = test_http_url_port_from_tracker() && all_ok;
    all_ok = test_http_url_root_path_when_no_slash() && all_ok;
    all_ok = test_http_url_rejects_https() && all_ok;
    all_ok = test_http_url_rejects_empty_host() && all_ok;
    all_ok = test_http_url_rejects_invalid_port() && all_ok;
    all_ok = test_http_url_rejects_zero_port() && all_ok;

    all_ok = test_ping_empty_target_returns_error() && all_ok;
    all_ok = test_ping_shell_injection_target_returns_error() && all_ok;
    all_ok = test_ping_slash_in_target_returns_error() && all_ok;
    all_ok = test_ping_space_in_target_returns_error() && all_ok;
    all_ok = test_unsupported_check_type_returns_error() && all_ok;
    all_ok = test_ping_leading_dash_target() && all_ok;
    all_ok = test_ping_pure_dash_target_windows() && all_ok;

    all_ok = test_http_check_2xx_returns_up() && all_ok;
    all_ok = test_http_check_5xx_returns_down() && all_ok;
    all_ok = test_http_check_4xx_boundary_returns_up() && all_ok;
    all_ok = test_http_check_unbound_local_port_returns_non_up() && all_ok;
    all_ok = test_http_check_timeout_returns_timeout() && all_ok;

    return finish_test("presence_check_runner_test", all_ok);
}
