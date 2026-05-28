#ifndef DB_DASHBOARD_REPOSITORY_HPP
#define DB_DASHBOARD_REPOSITORY_HPP

#include "database.hpp"
#include "scan_repository.hpp"
#include "dashboard/dashboard_types.hpp"

// REQUIRES: caller passes the sqlite3* handle from the enclosing
// Database::read()/write() access scope.
DashboardData read_dashboard_data(sqlite3* h, ScanRepository& repo);

#endif
