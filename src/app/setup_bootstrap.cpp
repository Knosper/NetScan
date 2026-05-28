#include "app/setup_bootstrap.hpp"

#include "app/db_bootstrap.hpp"
#include "app/paths.hpp"
#include "db/secret_repository.hpp"
#include "db/write_transaction.hpp"
#include "scan/target_validation.hpp"
#include "util/file_permissions.hpp"
#include "util/path_utils.hpp"
#include "util/secret_utils.hpp"
#include <cstdio>
#include <memory>
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#endif

namespace
{
const char* ADMIN_SECRET_NAME = "admin_api_key";
const char* RESTRICTED_SECRET_NAME = "restricted_api_key";

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
struct EvpPkeyDeleter
{
    void operator()(EVP_PKEY* value) const
    {
        EVP_PKEY_free(value);
    }
};

struct X509Deleter
{
    void operator()(X509* value) const
    {
        X509_free(value);
    }
};

struct X509ExtensionDeleter
{
    void operator()(X509_EXTENSION* value) const
    {
        X509_EXTENSION_free(value);
    }
};

struct FileCloser
{
    void operator()(std::FILE* value) const
    {
        std::fclose(value);
    }
};

typedef std::unique_ptr<EVP_PKEY, EvpPkeyDeleter> EvpPkeyPtr;
typedef std::unique_ptr<X509, X509Deleter> X509Ptr;
typedef std::unique_ptr<X509_EXTENSION, X509ExtensionDeleter> X509ExtensionPtr;
typedef std::unique_ptr<std::FILE, FileCloser> FilePtr;

struct CertificateFields
{
    std::string ip;
    int days = 365;
    EVP_PKEY* pkey = nullptr;
};
#endif

} // namespace

const char* admin_secret_name() { return ADMIN_SECRET_NAME; }
const char* restricted_secret_name() { return RESTRICTED_SECRET_NAME; }

bool parse_setup_host_port(const nlohmann::json& body, std::string& host, int& port,
                           std::string& error)
{
    std::string parsed_host = host;
    int parsed_port = port;

    if (body.contains("host"))
    {
        if (!body["host"].is_string())
        {
            error = "field 'host' must be a string";
            return false;
        }
        const std::string h = body["host"].get<std::string>();
        if (h.empty())
        {
            error = "field 'host' must not be empty";
            return false;
        }
        parsed_host = h;
    }

    if (body.contains("port"))
    {
        if (!body["port"].is_number_integer())
        {
            error = "field 'port' must be an integer";
            return false;
        }
        const int p = body["port"].get<int>();
        if (p < 1 || p > 65535)
        {
            error = "field 'port' must be between 1 and 65535";
            return false;
        }
        parsed_port = p;
    }

    host = parsed_host;
    port = parsed_port;
    return true;
}

bool is_supported_key_name(const std::string& key_name)
{
    return key_name == "admin" || key_name == "user";
}
bool is_readable_regular_file(const std::string& path, std::string& error)
{
    if (!path_exists(path))
    {
        error = path + " does not exist";
        return false;
    }

    if (!is_regular_file(path))
    {
        error = path + " is not a regular file";
        return false;
    }

    if (!is_readable(path))
    {
        error = path + " is not readable";
        return false;
    }

    return true;
}
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
EvpPkeyPtr make_rsa_key(Logger& logger)
{
    EvpPkeyPtr pkey(EVP_RSA_gen(2048));
    if (!pkey)
        logger.error("Failed to generate RSA key");
    return pkey;
}

void add_subject_alt_name(X509* cert, const std::string& ip)
{
    const std::string san = "IP:" + ip;
    X509V3_CTX ctx;
    X509V3_set_ctx_nodb(&ctx);
    X509V3_set_ctx(&ctx, cert, cert, nullptr, nullptr, 0);

    X509ExtensionPtr ext(
        X509V3_EXT_conf_nid(nullptr, &ctx, NID_subject_alt_name, san.c_str()));
    if (ext)
        X509_add_ext(cert, ext.get(), -1);
}

X509Ptr make_self_signed_cert(const CertificateFields& fields, Logger& logger)
{
    X509Ptr cert(X509_new());
    if (!cert)
    {
        logger.error("Failed to allocate X509");
        return cert;
    }

    ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), 1);
    X509_gmtime_adj(X509_get_notBefore(cert.get()), 0);
    X509_gmtime_adj(X509_get_notAfter(cert.get()), static_cast<long>(fields.days) * 86400L);
    X509_set_pubkey(cert.get(), fields.pkey);

    X509_NAME* name = X509_get_subject_name(cert.get());
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char*>("netscan"), -1, -1, 0);
    X509_set_issuer_name(cert.get(), name);
    add_subject_alt_name(cert.get(), fields.ip);
    X509_sign(cert.get(), fields.pkey, EVP_sha256());
    return cert;
}

bool write_private_key(const std::string& key_path, EVP_PKEY* pkey, Logger& logger)
{
    FilePtr file(std::fopen(key_path.c_str(), "wb"));
    if (!file || !PEM_write_PrivateKey(file.get(), pkey, nullptr, nullptr, 0, nullptr, nullptr))
    {
        logger.error("Failed to write " + key_path);
        return false;
    }

    file.reset();
    std::string permission_error;
    if (util::restrict_file_to_owner(key_path, &permission_error))
        return true;

    logger.error("Failed to restrict key file permissions: " + permission_error);
    return false;
}

bool write_certificate(const std::string& cert_path, X509* cert, Logger& logger)
{
    FilePtr file(std::fopen(cert_path.c_str(), "wb"));
    if (file && PEM_write_X509(file.get(), cert))
        return true;

    logger.error("Failed to write " + cert_path);
    return false;
}
#endif

bool build_and_write_self_signed_cert(const std::string& ip, const std::string& cert_path,
                                      const std::string& key_path, int days, Logger& logger)
{
#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
    (void)ip;
    (void)cert_path;
    (void)key_path;
    (void)days;
    logger.error("Build does not include TLS support — cannot generate self-signed certificate");
    return false;
#else
    EvpPkeyPtr pkey = make_rsa_key(logger);
    if (!pkey)
        return false;

    CertificateFields fields;
    fields.ip = ip;
    fields.days = days;
    fields.pkey = pkey.get();
    X509Ptr cert = make_self_signed_cert(fields, logger);
    if (!cert)
        return false;

    return write_private_key(key_path, pkey.get(), logger) &&
           write_certificate(cert_path, cert.get(), logger);
#endif
}

PersistedConfig make_setup_persisted_config()
{
    PersistedConfig persisted = make_default_persisted_config();
    persisted.ui_enabled = true;
    return persisted;
}
const char* secret_name_for_cli_key(const std::string& key_name)
{
    if (key_name == "admin")
        return admin_secret_name();
    if (key_name == "user")
        return restricted_secret_name();
    return nullptr;
}

struct KeyPersistContext
{
    SecretRepository& repo;
    SetupResult& result;
    Logger& logger;
};

static bool persist_keys_in_transaction(KeyPersistContext& ctx, sqlite3* h,
                                        const SetupPlan& plan, const char*& failed_secret_name)
{
    try
    {
        WriteTransaction tx(h, &ctx.logger);

        if (plan.generate_admin_key &&
            !persist_generated_api_key(ctx.repo, h, admin_secret_name(), ctx.result.admin_api_key))
        {
            failed_secret_name = admin_secret_name();
            ctx.logger.error(std::string("Failed to persist generated API key hash: ") +
                             failed_secret_name);
            return false;
        }

        if (plan.generate_user_key &&
            !persist_generated_api_key(ctx.repo, h, restricted_secret_name(),
                                       ctx.result.user_api_key))
        {
            failed_secret_name = restricted_secret_name();
            ctx.logger.error(std::string("Failed to persist generated API key hash: ") +
                             failed_secret_name);
            return false;
        }

        tx.commit();
        return true;
    }
    catch (const std::exception& ex)
    {
        ctx.logger.error(ex.what());
        return false;
    }
}

static void set_persist_error(SetupResult& result, const char* failed_secret_name)
{
    if (failed_secret_name == admin_secret_name())
        result.error = "failed to persist generated admin API key";
    else if (failed_secret_name == restricted_secret_name())
        result.error = "failed to persist generated restricted user API key";
    else
        result.error = "failed to persist generated API key";
}

static bool persist_generated_api_keys(const SetupPlan& plan, const AppConfig& runtime_config,
                                       Logger& logger, SetupResult& result)
{
    if (!plan.generate_admin_key && !plan.generate_user_key)
        return true;

    Database db(runtime_config.db_path);
    initialize_database(db, runtime_config, logger);

    if (plan.generate_user_key && !plan.generate_admin_key && !admin_api_key_exists(db))
    {
        result.error =
            "Cannot generate restricted user key: no admin API key exists. "
            "Set generateAdminKey=true or generate one first.";
        return false;
    }

    SecretRepository repo(db);
    KeyPersistContext ctx{repo, result, logger};
    const char* failed_secret_name = nullptr;
    const bool ok = db.write(
        [&](sqlite3* h) { return persist_keys_in_transaction(ctx, h, plan, failed_secret_name); });

    if (!ok)
    {
        set_persist_error(result, failed_secret_name);
        return false;
    }
    return true;
}

bool apply_setup_tls_plan(const SetupPlan& plan, const std::string& config_dir,
                                 PersistedConfig& persisted, Logger& logger, std::string& error)
{
    if (plan.tls_mode == SetupTlsMode::Disabled)
        return true;

    persisted.tls_enabled = true;
    if (plan.tls_mode == SetupTlsMode::ExistingFiles)
    {
        persisted.tls_cert_path = plan.tls_cert_path;
        persisted.tls_key_path = plan.tls_key_path;
        return true;
    }

    const std::string cert_path = resolve_path(config_dir, default_tls_cert_path());
    const std::string key_path = resolve_path(config_dir, default_tls_key_path());
    const std::string certs_dir = dir_of(cert_path);
    std::string directory_error;
    if (!ensure_directory_exists(certs_dir, &directory_error))
    {
        logger.error(directory_error);
        error = "failed to create certificate directory";
        return false;
    }

    if (!build_and_write_self_signed_cert(plan.host, cert_path, key_path, 365, logger))
    {
        error = "failed to generate self-signed certificate";
        return false;
    }

    persisted.tls_cert_path = default_tls_cert_path();
    persisted.tls_key_path = default_tls_key_path();
    return true;
}

bool validate_setup_tls_files(const PersistedConfig& persisted,
                                     const std::string& absolute_config_path, std::string& error)
{
    if (!persisted.tls_enabled)
        return true;

    const AppConfig runtime_config = resolve_runtime_config(persisted, absolute_config_path);
    if (!is_readable_regular_file(runtime_config.tls_cert_path, error))
        return false;
    if (!is_readable_regular_file(runtime_config.tls_key_path, error))
        return false;
    return true;
}

// Applies the shared setup side effects for both CLI and browser setup:
// build/validate persisted config, write it atomically, and optionally persist API keys.
SetupResult apply_setup_plan(const SetupPlan& plan, const std::string& config_path,
                                    Logger& logger)
{
    SetupResult result;
    const std::string absolute_config_path = to_absolute_path(config_path);
    const std::string config_dir = dir_of(absolute_config_path);

    std::string directory_error;
    if (!ensure_directory_exists(config_dir, &directory_error))
    {
        logger.error(directory_error);
        result.error = "failed to create config directory";
        return result;
    }

    PersistedConfig persisted = make_setup_persisted_config();
    persisted.host = plan.host;
    persisted.port = plan.port;

    if (!apply_setup_tls_plan(plan, config_dir, persisted, logger, result.error))
        return result;

    if (!validate_persisted_config_for_startup(persisted, result.error))
        return result;
    const AppConfig runtime_config = resolve_runtime_config(persisted, absolute_config_path);
    if (!validate_setup_tls_files(persisted, absolute_config_path, result.error))
        return result;
    if (!persist_generated_api_keys(plan, runtime_config, logger, result))
        return result;
    if (!write_persisted_config_atomically(absolute_config_path, persisted, result.error))
        return result;

    result.ok = true;
    return result;
}
