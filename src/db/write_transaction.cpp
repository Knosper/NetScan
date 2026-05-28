#include "db/write_transaction.hpp"
#include "db/sqlite_helpers.hpp"
#include "util/logger.hpp"

#include <iostream>
#include <stdexcept>

WriteTransaction::WriteTransaction(sqlite3* h, Logger* logger)
    : h_(h), logger_(logger), committed_(false)
{
    std::string error;
    if (!db_exec(h_, "BEGIN IMMEDIATE;", &error))
        throw std::runtime_error("WriteTransaction: failed to begin transaction: " + error);
}

WriteTransaction::~WriteTransaction()
{
    if (committed_)
        return;

    std::string error;
    if (db_exec(h_, "ROLLBACK;", &error))
        return;

    std::string message = "WriteTransaction: failed to rollback transaction";
    if (!error.empty())
        message += ": " + error;

    try
    {
        if (logger_ != nullptr)
        {
            logger_->warn(message);
            return;
        }
    }
    catch (...)
    {
    }

    std::cerr << "WARN " << message << "\n";
}

void WriteTransaction::commit()
{
    std::string error;
    if (!db_exec(h_, "COMMIT;", &error))
        throw std::runtime_error("WriteTransaction: failed to commit transaction: " + error);
    committed_ = true;
}
