#include "db/database.hpp"
#include "util/file_permissions.hpp"
#include "util/logger.hpp"
#include "util/path_utils.hpp"
#include <stdexcept>
#include <vector>

namespace
{
constexpr int kDefaultBusyTimeoutMs = 5000;

sqlite3* open_database_handle(const std::string& path, const char* open_context)
{
    sqlite3* handle = nullptr;
    if (sqlite3_open(path.c_str(), &handle) != SQLITE_OK)
    {
        const std::string error = handle ? sqlite3_errmsg(handle) : "unknown sqlite error";
        if (handle != nullptr)
            sqlite3_close(handle);
        throw std::runtime_error(std::string(open_context) + ": " + error);
    }
    return handle;
}

void apply_busy_timeout(sqlite3* db, const char* error_context)
{
    if (sqlite3_busy_timeout(db, kDefaultBusyTimeoutMs) != SQLITE_OK)
        throw std::runtime_error(std::string(error_context) + ": " + sqlite3_errmsg(db));
}

std::string query_pragma_text(sqlite3* db, const char* sql, const char* error_context)
{
    Stmt stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt.ptr, nullptr) != SQLITE_OK)
        throw std::runtime_error(std::string(error_context) + ": " + sqlite3_errmsg(db));

    std::string value;
    const int rc = sqlite3_step(stmt.ptr);
    if (rc == SQLITE_ROW)
    {
        const unsigned char* text = sqlite3_column_text(stmt.ptr, 0);
        value = text ? reinterpret_cast<const char*>(text) : "";
    }
    else if (rc != SQLITE_DONE)
    {
        throw std::runtime_error(std::string(error_context) + ": " + sqlite3_errmsg(db));
    }

    return value;
}
} // namespace

Database::Database(const std::string& path) : path_(path), write_db_(nullptr), read_db_(nullptr)
{
    write_db_ = open_database_handle(path, "Failed to open writer database handle");
    try
    {
        apply_busy_timeout(write_db_, "Failed to initialize writer busy timeout");
        read_db_ = open_database_handle(path, "Failed to open reader database handle");
        apply_busy_timeout(read_db_, "Failed to initialize reader busy timeout");
    }
    catch (...)
    {
        if (read_db_ != nullptr)
            sqlite3_close(read_db_);
        if (write_db_ != nullptr)
            sqlite3_close(write_db_);
        read_db_ = nullptr;
        write_db_ = nullptr;
        throw;
    }
}

Database::~Database()
{
    if (read_db_ != nullptr)
        sqlite3_close(read_db_);
    if (write_db_ != nullptr)
        sqlite3_close(write_db_);
}

bool Database::exec_sql(sqlite3* h, const char* sql, std::string* error_message)
{
    char* error = nullptr;
    const int rc = sqlite3_exec(h, sql, nullptr, nullptr, &error);
    if (rc == SQLITE_OK)
        return true;

    if (error_message != nullptr)
        *error_message = error ? error : "unknown sqlite error";
    sqlite3_free(error);
    return false;
}

int Database::changes(sqlite3* h) const
{
    return sqlite3_changes(h);
}

int Database::last_insert_rowid(sqlite3* h) const
{
    return static_cast<int>(sqlite3_last_insert_rowid(h));
}

std::string Database::last_error() const
{
    return sqlite3_errmsg(write_db_);
}

const std::string& Database::path() const
{
    return path_;
}

std::unique_lock<std::mutex> Database::lock() const
{
    return std::unique_lock<std::mutex>(write_mutex_);
}

namespace
{
void harden_db_file_permissions(const std::string& db_path, Logger& logger)
{
    const std::vector<std::string> candidates = {
        db_path,
        db_path + "-wal",
        db_path + "-shm"
    };

    for (std::vector<std::string>::const_iterator it = candidates.begin();
         it != candidates.end(); ++it)
    {
        if (!path_exists(*it))
            continue;

        std::string error;
        if (!util::restrict_file_to_owner(*it, &error))
            logger.warn("Database file permission hardening failed for " + *it + ": " + error);
    }
}

void configure_writer(Database& db, Logger& logger)
{
    logger.info("Configuring SQLite runtime (writer): requested journal_mode=WAL");

    const std::string effective_mode = db.write(
        [](sqlite3* h)
        {
            return query_pragma_text(h, "PRAGMA journal_mode=WAL;",
                                     "failed to configure writer SQLite journal mode");
        });
    logger.info("SQLite runtime (writer): effective journal_mode=" + effective_mode);

    if (effective_mode != "wal")
    {
        throw std::runtime_error(
            "SQLite writer runtime configuration failed: expected journal_mode=wal, got " +
            effective_mode);
    }

    db.write([](sqlite3* h)
             { apply_busy_timeout(h, "failed to configure writer SQLite busy timeout"); });
}

void configure_reader(Database& db, Logger& logger)
{
    db.read([](sqlite3* h)
            { apply_busy_timeout(h, "failed to configure reader SQLite busy timeout"); });

    const std::string reader_mode = db.read(
        [](sqlite3* h)
        {
            return query_pragma_text(h, "PRAGMA journal_mode;",
                                     "failed to verify reader SQLite journal mode");
        });
    logger.info("SQLite runtime (reader): verified journal_mode=" + reader_mode);

    if (reader_mode != "wal")
    {
        throw std::runtime_error(
            "SQLite reader runtime verification failed: expected journal_mode=wal, got " +
            reader_mode);
    }
}
} // namespace

void configure_database_runtime(Database& db, Logger& logger)
{
    configure_writer(db, logger);
    configure_reader(db, logger);
}

void harden_database_file_permissions(Database& db, Logger& logger)
{
    harden_db_file_permissions(db.path(), logger);
}
