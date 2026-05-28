#include "platform.hpp"
#include <chrono>
#include <iostream>
#include <thread>
#include "app/application_bootstrap.hpp"
#include "app/config.hpp"
#include "app/paths.hpp"
#include "app/windows_single_instance.hpp"
#include "cxxopts/cxxopts.hpp"
#include "util/logger.hpp"
#include "util/path_utils.hpp"

struct ParsedCli
{
    std::string config_path;
    CliOverrides overrides;
    bool setup = false;
    bool generate_key = false;
    std::string generate_key_name;
    bool generate_cert = false;
    GenerateCertOptions cert_opts;
};

static cxxopts::Options build_cli_options(const std::string& default_config)
{
    cxxopts::Options options("netscan", "NetScan Server");
    options.add_options()
        ("c,config", "Path to config file",
            cxxopts::value<std::string>()->default_value(default_config))
        ("setup", "Run interactive setup wizard for binding/TLS/auth")
        ("generate-key", "Generate and persist an API key hash for admin or user",
            cxxopts::value<std::string>())
        ("generate-cert", "Generate a self-signed TLS certificate and key")
        ("ip", "IP address for certificate SAN (used with --generate-cert)",
            cxxopts::value<std::string>()->default_value("127.0.0.1"))
        ("days", "Certificate validity in days (used with --generate-cert)",
            cxxopts::value<int>()->default_value("365"))
        ("force", "Overwrite existing certificate files (used with --generate-cert)")
        ("ui", "Enable web UI (overrides config)")
        ("api-only", "Disable web UI, API only (overrides config)")
        ("web-dir", "Path to web assets directory (overrides config)",
            cxxopts::value<std::string>())
        ("h,help", "Show help");
    return options;
}

static void validate_cli_conflicts(const cxxopts::ParseResult& result)
{
    if (result.count("ui") && result.count("api-only"))
        throw std::runtime_error("Cannot use --ui and --api-only together");

    if (result.count("generate-key") &&
        (result.count("ui") || result.count("api-only") || result.count("web-dir")))
        throw std::runtime_error("Cannot combine --generate-key with server override flags");

    if (result.count("setup") &&
        (result.count("ui") || result.count("api-only") || result.count("web-dir") ||
         result.count("generate-key") || result.count("generate-cert")))
    {
        throw std::runtime_error("Cannot combine --setup with other command modes or server override flags");
    }

    if (result.count("generate-cert") && result.count("generate-key"))
        throw std::runtime_error("Cannot combine --generate-cert with --generate-key");

    if (result.count("generate-cert") &&
        (result.count("ui") || result.count("api-only") || result.count("web-dir")))
        throw std::runtime_error("Cannot combine --generate-cert with server override flags");
}

static void map_cli_overrides(const cxxopts::ParseResult& result, ParsedCli& out)
{
    out.config_path = result["config"].as<std::string>();
    out.setup = result.count("setup") > 0;

    if (result.count("ui"))
    {
        out.overrides.ui_set = true;
        out.overrides.ui_value = true;
    }

    if (result.count("api-only"))
    {
        out.overrides.ui_set = true;
        out.overrides.ui_value = false;
    }

    if (result.count("web-dir"))
    {
        out.overrides.web_dir_set = true;
        out.overrides.web_dir = result["web-dir"].as<std::string>();
    }

    if (result.count("generate-key"))
    {
        out.generate_key = true;
        out.generate_key_name = result["generate-key"].as<std::string>();
    }

    if (result.count("generate-cert"))
    {
        out.generate_cert = true;
        out.cert_opts.ip_set = result.count("ip") > 0;
        out.cert_opts.ip     = result["ip"].as<std::string>();
        out.cert_opts.days   = result["days"].as<int>();
        out.cert_opts.force  = result.count("force") > 0;
    }
}

// Runs bootstrap in a loop, restarting on EXIT_RESTART. Aborts only if bootstrap
// keeps crashing immediately (under kHealthyRunMin) more than kMaxCrashRestarts
// times in a row — protects against a tight spin without limiting legitimate
// user-triggered restarts (which always take longer than the crash threshold).
static int run_with_restart(ApplicationBootstrap& bootstrap, const ParsedCli& cli, Logger& logger)
{
    constexpr int kMaxCrashRestarts = 5;
    constexpr auto kHealthyRunMin = std::chrono::seconds(1);
    constexpr auto kRestartDelay = std::chrono::milliseconds(500);

    int consecutive_crashes = 0;

    for (;;)
    {
        const auto run_started = std::chrono::steady_clock::now();
        const int result = bootstrap.run(cli.config_path, cli.overrides, logger);
        if (result != EXIT_RESTART)
            return result;

        const auto run_duration = std::chrono::steady_clock::now() - run_started;
        if (run_duration < kHealthyRunMin)
        {
            if (++consecutive_crashes > kMaxCrashRestarts)
            {
                logger.error("Bootstrap crashed too many times in a row; aborting.");
                return 1;
            }
        }
        else
        {
            consecutive_crashes = 0;
        }

        logger.info("Restarting NetScan...");
        std::this_thread::sleep_for(kRestartDelay);
    }
}

// Returns 0 on --help (exit 0), 1 on success. Throws std::runtime_error on conflict.
static int parse_cli(int argc, char* argv[], ParsedCli& out)
{
    const std::string default_config = get_default_config_path();
    auto options = build_cli_options(default_config);
    auto result = options.parse(argc, argv);

    if (result.count("help"))
    {
        std::cout << options.help() << std::endl;
        return 0;
    }

    validate_cli_conflicts(result);

    map_cli_overrides(result, out);
    return 1;
}

int main(int argc, char* argv[])
{
    Logger logger;
    logger.add_sink(std::unique_ptr<LogSink>(new ConsoleLogSink()));

    try
    {
        ParsedCli cli;
        const int parsed = parse_cli(argc, argv, cli);
        if (parsed == 0)
            return 0;

        ApplicationBootstrap bootstrap;
        if (cli.setup)
            return bootstrap.run_setup(cli.config_path, logger);
        if (cli.generate_key)
            return bootstrap.generate_key(cli.config_path, cli.generate_key_name, logger);
        if (cli.generate_cert)
            return bootstrap.generate_cert(cli.config_path, cli.cert_opts, logger);

#ifdef _WIN32
        WindowsSingleInstanceGuard instance_guard;
        const std::string install_root = dir_of(to_absolute_path(cli.config_path));
        if (!instance_guard.acquire_for_install_root(install_root))
        {
            logger.info("NetScan server is already running.");
            return 1;
        }
#endif

        return run_with_restart(bootstrap, cli, logger);
    }
    catch (const std::exception& e)
    {
        logger.error(e.what());
        return 1;
    }
}
