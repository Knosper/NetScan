#include "app/config.hpp"
#include "test_output.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

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

std::string write_tmp(const std::string& name, const std::string& content)
{
#ifdef _WIN32
    char temp_dir[MAX_PATH + 1] = {0};
    const DWORD temp_dir_len = GetTempPathA(MAX_PATH, temp_dir);
    if (temp_dir_len == 0 || temp_dir_len > MAX_PATH)
        return "";

    char temp_file[MAX_PATH + 1] = {0};
    if (GetTempFileNameA(temp_dir, "nsc", 0, temp_file) == 0)
        return "";

    const std::string path = std::string(temp_file) + "-" + name;
    DeleteFileA(temp_file);
#else
    const std::string path = "/tmp/" + name;
#endif

    std::ofstream f(path.c_str(), std::ios::out | std::ios::trunc | std::ios::binary);
    f << content;
    if (!f.good())
        return "";
    return path;
}

bool throws_on_load(const std::string& path)
{
    try
    {
        load_persisted_config(path);
        return false;
    }
    catch (const std::runtime_error&)
    {
        return true;
    }
}

bool test_is_loopback_host()
{
    bool ok = true;

    ok = expect(is_loopback_host("127.0.0.1"), "127.0.0.1 is loopback") && ok;
    ok = expect(is_loopback_host("127.255.255.255"), "127.255.255.255 is loopback") && ok;
    ok = expect(is_loopback_host("127.0.0.2"), "127.0.0.2 is loopback") && ok;
    ok = expect(is_loopback_host("::1"), "::1 is loopback") && ok;

    ok = expect(!is_loopback_host("127.foo"), "127.foo is not loopback") && ok;
    ok = expect(!is_loopback_host("127.example.com"), "127.example.com is not loopback") && ok;
    ok = expect(!is_loopback_host("127.0.0.0/24"), "127.0.0.0/24 is not loopback") && ok;
    ok = expect(!is_loopback_host(""), "empty string is not loopback") && ok;
    ok = expect(!is_loopback_host("localhost"), "localhost hostname is not loopback") && ok;
    ok = expect(!is_loopback_host("192.168.1.1"), "192.168.1.1 is not loopback") && ok;
    ok = expect(!is_loopback_host("128.0.0.1"), "128.0.0.1 is not loopback") && ok;

    return ok;
}

bool test_serialize_persisted_config_emits_tls_paths()
{
    PersistedConfig persisted = make_default_persisted_config();
    persisted.host = "192.168.1.10";
    persisted.ui_enabled = true;
    persisted.tls_enabled = true;
    persisted.tls_cert_path = "./certs/netscan.crt";
    persisted.tls_key_path = "./certs/netscan.key";

    const std::string serialized = serialize_persisted_config(persisted);

    bool ok = true;
    ok = expect(serialized.find("host=192.168.1.10\n") != std::string::npos,
                "serialized config includes host") && ok;
    ok = expect(serialized.find("ui_enabled=true\n") != std::string::npos,
                "serialized config includes ui_enabled") && ok;
    ok = expect(serialized.find("tls_enabled=true\n") != std::string::npos,
                "serialized config includes tls_enabled") && ok;
    ok = expect(serialized.find("tls_cert_path=./certs/netscan.crt\n") != std::string::npos,
                "serialized config includes tls cert path") && ok;
    ok = expect(serialized.find("tls_key_path=./certs/netscan.key\n") != std::string::npos,
                "serialized config includes tls key path") && ok;
    return ok;
}
bool test_parse_errors()
{
    bool ok = true;

    ok = expect(throws_on_load(write_tmp("cfg_no_eq.ini", "host\n")),
                "missing '=' throws") && ok;

    ok = expect(throws_on_load(write_tmp("cfg_empty_key.ini", "=somevalue\n")),
                "empty key throws") && ok;

    ok = expect(throws_on_load(write_tmp("cfg_port_empty.ini", "port=\n")),
                "port= empty throws") && ok;

    ok = expect(throws_on_load(write_tmp("cfg_port_alpha.ini", "port=abc\n")),
                "port=abc throws") && ok;

    ok = expect(throws_on_load(write_tmp("cfg_port_oor.ini", "port=99999\n")),
                "port=99999 out-of-range throws") && ok;

    return ok;
}

bool test_round_trip()
{
    PersistedConfig original = make_default_persisted_config();
    original.host = "192.168.0.5";
    original.port = 9090;
    original.db_path = "./data.db";
    original.web_dir = "./web";
    original.log_level = "debug";
    original.ui_enabled = true;
    original.scan_cooldown_seconds = 30;
    original.tls_enabled = true;
    original.tls_cert_path = "./certs/c.crt";
    original.tls_key_path = "./certs/c.key";

    const std::string path = write_tmp("cfg_round_trip.ini",
                                       serialize_persisted_config(original));
    const PersistedConfig loaded = load_persisted_config(path);

    bool ok = true;
    ok = expect(loaded.host == original.host,           "round-trip host") && ok;
    ok = expect(loaded.port == original.port,           "round-trip port") && ok;
    ok = expect(loaded.db_path == original.db_path,     "round-trip db_path") && ok;
    ok = expect(loaded.web_dir == original.web_dir,     "round-trip web_dir") && ok;
    ok = expect(loaded.log_level == original.log_level, "round-trip log_level") && ok;
    ok = expect(loaded.ui_enabled == original.ui_enabled,
                "round-trip ui_enabled") && ok;
    ok = expect(loaded.scan_cooldown_seconds == original.scan_cooldown_seconds,
                "round-trip scan_cooldown_seconds") && ok;
    ok = expect(loaded.tls_enabled == original.tls_enabled,
                "round-trip tls_enabled") && ok;
    ok = expect(loaded.tls_cert_path == original.tls_cert_path,
                "round-trip tls_cert_path") && ok;
    ok = expect(loaded.tls_key_path == original.tls_key_path,
                "round-trip tls_key_path") && ok;
    return ok;
}

bool test_validate_persisted_config_required_fields()
{
    bool ok = true;
    std::string error;

    PersistedConfig base = make_default_persisted_config();

    PersistedConfig no_host = base;
    no_host.host = "";
    ok = expect(!validate_persisted_config_for_startup(no_host, error),
                "empty host fails validation") && ok;

    PersistedConfig no_log_level = base;
    no_log_level.log_level = "";
    ok = expect(!validate_persisted_config_for_startup(no_log_level, error),
                "empty log_level fails validation") && ok;

    PersistedConfig no_db_path = base;
    no_db_path.db_path = "";
    ok = expect(!validate_persisted_config_for_startup(no_db_path, error),
                "empty db_path fails validation") && ok;

    PersistedConfig no_web_dir = base;
    no_web_dir.web_dir = "";
    ok = expect(!validate_persisted_config_for_startup(no_web_dir, error),
                "empty web_dir fails validation") && ok;

    return ok;
}

bool test_apply_cli_overrides()
{
    AppConfig base = make_default_config();
    base.ui_enabled = false;
    base.web_dir = "/original/web";

    bool ok = true;

    CliOverrides none;
    AppConfig r1 = apply_cli_overrides(base, none);
    ok = expect(!r1.ui_enabled,              "no override: ui_enabled unchanged") && ok;
    ok = expect(r1.web_dir == "/original/web","no override: web_dir unchanged") && ok;

    CliOverrides ui_only;
    ui_only.ui_set = true;
    ui_only.ui_value = true;
    AppConfig r2 = apply_cli_overrides(base, ui_only);
    ok = expect(r2.ui_enabled,               "ui_set=true applies") && ok;
    ok = expect(r2.web_dir == "/original/web","ui_set: web_dir unchanged") && ok;

    CliOverrides wd_only;
    wd_only.web_dir_set = true;
    wd_only.web_dir = "/custom/web";
    AppConfig r3 = apply_cli_overrides(base, wd_only);
    ok = expect(!r3.ui_enabled,              "web_dir_set: ui_enabled unchanged") && ok;
    ok = expect(r3.web_dir == "/custom/web", "web_dir_set applies") && ok;

    CliOverrides both;
    both.ui_set = true;
    both.ui_value = true;
    both.web_dir_set = true;
    both.web_dir = "/both/web";
    AppConfig r4 = apply_cli_overrides(base, both);
    ok = expect(r4.ui_enabled,              "both set: ui_enabled applies") && ok;
    ok = expect(r4.web_dir == "/both/web",  "both set: web_dir applies") && ok;

    return ok;
}

} // namespace

int main()
{
    bool all_ok = true;
    all_ok = test_is_loopback_host() && all_ok;
    all_ok = test_serialize_persisted_config_emits_tls_paths() && all_ok;
    all_ok = test_parse_errors() && all_ok;
    all_ok = test_round_trip() && all_ok;
    all_ok = test_validate_persisted_config_required_fields() && all_ok;
    all_ok = test_apply_cli_overrides() && all_ok;
    return finish_test("config_test", all_ok);
}
