#include "app/application_bootstrap.hpp"

#include "app/app_context.hpp"
#include "app/auth_config.hpp"
#include "app/db_bootstrap.hpp"
#include "app/http_bootstrap.hpp"
#include "app/paths.hpp"
#include "app/service_wiring.hpp"
#include "app/setup_bootstrap.hpp"
#include "app/shutdown_controller.hpp"
#include "db/database.hpp"
#include "http/server.hpp"
#include "util/path_utils.hpp"
#include "util/secret_utils.hpp"
#include <iostream>
#include <stdexcept>

int ApplicationBootstrap::run(const std::string& config_path, const CliOverrides& overrides,
                               Logger& logger)
{
    const std::string absolute_config_path = to_absolute_path(config_path);
    if (!path_exists(absolute_config_path))
    {
        logger.info("No config file found — starting browser-based setup");
        return run_setup_server(config_path, logger);
    }

    // load_persisted_config_safe resets a malformed conf.ini to defaults with
    // setup_pending=true and rewrites it atomically. This guarantees we never
    // boot from an untrusted config — any parse/validation error drops us into
    // setup mode instead of crashing.
    if (load_persisted_config_safe(absolute_config_path, logger).setup_pending)
    {
        logger.info("Config marked as setup-pending — starting browser-based setup");
        return run_setup_server(config_path, logger);
    }

    const AppConfig config = load_and_configure(config_path, overrides, logger);

    Database db(config.db_path);
    initialize_database(db, config, logger);
    const AuthConfig auth_config = load_auth_config(db);
    validate_startup(config, auth_config, logger);

    // Database must outlive ScanService: scan workers may persist terminal state
    // while ScanService is being destroyed during shutdown.
    {
        ServicesBundleDeps service_deps{db, config, config_path, logger};
        ServicesBundle services(service_deps);
        AppContext ctx{config, logger, absolute_config_path};
        Server server(ctx, auth_config, services.api_services);

        ShutdownController shutdown;
        if (!shutdown.setup(logger))
            return 1;

        services.scheduler_service.start();
        services.presence_scheduler_service.start();
        ServerRunContext run_ctx{server, services.scan_service, services.scheduler_service,
                                 services.presence_scheduler_service, config, shutdown,
                                 logger, absolute_config_path};
        return run_server(run_ctx);
    }

}
int ApplicationBootstrap::generate_key(const std::string& config_path, const std::string& key_name,
                                       Logger& logger)
{
    if (!is_supported_key_name(key_name))
        throw std::runtime_error("CLI usage: ./netscan --generate-key <admin|user>");

    logger.set_level(LogLevel::Error);
    const AppConfig config = load_and_configure(config_path, CliOverrides(), logger);

    Database db(config.db_path);
    initialize_database(db, config, logger);

    if (key_name == "user" && !admin_api_key_exists(db))
        throw std::runtime_error(
            "Cannot generate restricted user key: no admin API key exists. "
            "Run './netscan --generate-key admin' first.");

    SecretRepository repo(db);
    std::string secret;
    const bool ok = db.write(
        [&repo, &key_name, &secret](sqlite3* h)
        { return persist_generated_api_key(repo, h, secret_name_for_cli_key(key_name), secret); });
    if (!ok)
        throw std::runtime_error("Failed to persist generated API key hash");

    std::cerr << "Save this key now — it cannot be retrieved later. Only the salted hash is stored."
              << std::endl;
    std::cout << secret << std::endl;
    return 0;

}
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
struct CertPaths
{
    std::string cert_path;
    std::string key_path;
    std::string certs_dir;
};

static int prepare_and_build_cert(const GenerateCertOptions& opts, const CertPaths& paths,
                                   Logger& logger)
{
    if (!opts.force && (path_exists(paths.cert_path) || path_exists(paths.key_path)))
    {
        logger.error("Certificate files already exist; use --force to overwrite: " +
                     paths.certs_dir);
        return 1;
    }

    std::string directory_error;
    if (!ensure_directory_exists(paths.certs_dir, &directory_error))
    {
        logger.error(directory_error);
        return 1;
    }

    if (!build_and_write_self_signed_cert(opts.ip, paths.cert_path, paths.key_path, opts.days,
                                          logger))
        return 1;

    return 0;
}
#endif

int ApplicationBootstrap::generate_cert(const std::string& config_path,
                                        const GenerateCertOptions& opts, Logger& logger)
{
#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
    (void)config_path;
    (void)opts;
    logger.error("--generate-cert requires a build with CPPHTTPLIB_OPENSSL_SUPPORT");
    return 1;
#else
    logger.set_level(LogLevel::Error);

    const PersistedConfig persisted = load_persisted_config(config_path);
    const std::string conf_host     = persisted.host;
    const std::string config_dir    = dir_of(to_absolute_path(config_path));

    GenerateCertOptions resolved_opts = opts;
    if (!opts.ip_set)
        resolved_opts.ip = conf_host;
    else if (opts.ip != conf_host)
        std::cout << "Warning: --ip " << opts.ip << " does not match conf.ini host=" << conf_host
                  << "; the certificate SAN will not match the bind address.\n\n";

    const std::string cert_path = resolve_path(config_dir, default_tls_cert_path());
    const CertPaths paths{cert_path, resolve_path(config_dir, default_tls_key_path()),
                          dir_of(cert_path)};

    const int rc = prepare_and_build_cert(resolved_opts, paths, logger);
    if (rc != 0)
        return rc;

    std::cout << "Generated:\n"
              << "  " << paths.cert_path << "\n"
              << "  " << paths.key_path << "\n";

    PersistedConfig persisted_rw = load_persisted_config(config_path);
    persisted_rw.tls_enabled    = true;
    persisted_rw.tls_cert_path  = default_tls_cert_path();
    persisted_rw.tls_key_path   = default_tls_key_path();

    std::string write_error;
    if (write_persisted_config_atomically(config_path, persisted_rw, write_error))
        std::cout << "\nconf.ini updated: tls_enabled=true\n";
    else
        std::cout << "\nWarning: could not update conf.ini: " << write_error
                  << "\nSet manually: tls_enabled=true, tls_cert_path="
                  << default_tls_cert_path() << ", tls_key_path="
                  << default_tls_key_path() << "\n";

    return 0;

#endif
}
