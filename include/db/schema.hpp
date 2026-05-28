#ifndef DB_SCHEMA_HPP
#define DB_SCHEMA_HPP

#include "database.hpp"
#include "util/logger.hpp"

// Initialises the database schema (idempotent; no migration — requires a clean DB on schema changes).
// Must be called once before the HTTP server starts accepting requests
// (single-threaded context - no Database::lock() required).
void init_schema(Database& db, Logger& logger);

#endif
