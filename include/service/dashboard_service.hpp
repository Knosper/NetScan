#ifndef SERVICE_DASHBOARD_SERVICE_HPP
#define SERVICE_DASHBOARD_SERVICE_HPP

#include "dashboard/dashboard_types.hpp"
#include "db/database.hpp"
#include "db/scan_repository.hpp"

class DashboardService
{
public:
    explicit DashboardService(Database& db);
    DashboardData get_dashboard() const;

private:
    Database& db_;
    mutable ScanRepository repo_;
};

#endif
