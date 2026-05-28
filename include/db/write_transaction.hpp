#ifndef DB_WRITE_TRANSACTION_HPP
#define DB_WRITE_TRANSACTION_HPP

#include <sqlite3.h>
#include "util/logger.hpp"

// RAII guard that wraps BEGIN IMMEDIATE / COMMIT / ROLLBACK.
//
// Usage:
//   db_.write([&](sqlite3* h) {
//       WriteTransaction tx(h, &logger_);
//       repo_.insert_a(h, ...);
//       repo_.insert_b(h, ...);
//       tx.commit();   // atomically commits; destructor no-ops
//   });
//
// REQUIRES: constructed inside a Database::write() scope with the handle
// passed as first argument. If commit() is never called, the destructor
// executes ROLLBACK.
class WriteTransaction
{
public:
    explicit WriteTransaction(sqlite3* h, Logger* logger = nullptr);
    ~WriteTransaction();

    WriteTransaction(const WriteTransaction&) = delete;
    WriteTransaction& operator=(const WriteTransaction&) = delete;
    WriteTransaction(WriteTransaction&&) = delete;
    WriteTransaction& operator=(WriteTransaction&&) = delete;

    // Executes COMMIT and marks this transaction as committed.
    // Throws std::runtime_error on failure.
    void commit();

private:
    sqlite3* h_;
    Logger*  logger_;
    bool     committed_;
};

#endif
