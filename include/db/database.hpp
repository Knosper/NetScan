#ifndef DATABASE_HPP
#define DATABASE_HPP

#include <mutex>
#include <string>
#include <sqlite3.h>
#include <utility>

#include "util/logger.hpp"

struct Stmt
{
    sqlite3_stmt* ptr = nullptr;
    Stmt() = default;

    ~Stmt()
    {
        if (ptr)
            sqlite3_finalize(ptr);
    }

    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;
};

// Concurrency policy
// ==================
// This project currently uses one writer connection and one reader connection.
// Callers should prefer Database::read(...) and Database::write(...)
// so the DB layer owns synchronization instead of pushing lock ownership
// into services and repositories.
//
// The callback receives the active sqlite3* handle as its sole parameter:
//
//   db_.read([](sqlite3* h)  { ... });
//   db_.write([](sqlite3* h) { ... });
//
// lock() is retained as an escape hatch for schema/startup code and direct
// call sites that require explicit lock ownership.

class Database
{
private:
    std::string path_;
    sqlite3* write_db_;
    sqlite3* read_db_;
    mutable std::mutex read_mutex_;
    mutable std::mutex write_mutex_;

    // Internal access seams for read/write handle and lock selection.
    std::mutex& read_access_mutex() const
    {
        return read_mutex_;
    }
    std::mutex& write_access_mutex()
    {
        return write_mutex_;
    }
    sqlite3* read_access_connection() const
    {
        return read_db_;
    }
    sqlite3* write_access_connection()
    {
        return write_db_;
    }

    template <typename Fn>
    auto run_read_access(Fn&& fn) const -> decltype(fn(static_cast<sqlite3*>(nullptr)))
    {
        std::unique_lock<std::mutex> guard(read_access_mutex());
        return std::forward<Fn>(fn)(read_access_connection());
    }

    template <typename Fn>
    auto run_write_access(Fn&& fn) -> decltype(fn(static_cast<sqlite3*>(nullptr)))
    {
        std::unique_lock<std::mutex> guard(write_access_mutex());
        return std::forward<Fn>(fn)(write_access_connection());
    }

public:
    Database(const std::string& path);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // exec_sql executes a one-shot SQL statement on the writer connection.
    // REQUIRES: caller holds the writer lock (i.e. executes inside write()).
    bool exec_sql(sqlite3* h, const char* sql, std::string* error_message = nullptr);

    int changes(sqlite3* h) const;
    int last_insert_rowid(sqlite3* h) const;
    std::string last_error() const;
    const std::string& path() const;

    // read(fn): acquires the reader lock and calls fn(sqlite3* h).
    // The callback runs synchronously on the calling thread.
    // DB access must not escape this scope to another thread.
    template <typename Fn>
    auto read(Fn&& fn) const -> decltype(fn(static_cast<sqlite3*>(nullptr)))
    {
        return run_read_access(std::forward<Fn>(fn));
    }

    // write(fn): acquires the writer lock and calls fn(sqlite3* h).
    // The callback runs synchronously on the calling thread.
    // DB access must not escape this scope to another thread.
    template <typename Fn>
    auto write(Fn&& fn) -> decltype(fn(static_cast<sqlite3*>(nullptr)))
    {
        return run_write_access(std::forward<Fn>(fn));
    }

    // Acquires the writer-side mutex and returns a scoped lock.
    // Transitional escape hatch: prefer read()/write() for new code.
    std::unique_lock<std::mutex> lock() const;
};

// Configures the SQLite runtime mode for the startup writer connection.
// Must be called once after Database construction and before request handling begins.
void configure_database_runtime(Database& db, Logger& logger);

// Re-applies owner-only permissions to the database file and SQLite WAL sidecars.
// Warns on failure but does not throw for permission-hardening errors.
void harden_database_file_permissions(Database& db, Logger& logger);

#endif
