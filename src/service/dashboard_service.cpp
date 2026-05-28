#include "service/dashboard_service.hpp"
#include "db/dashboard_repository.hpp"

DashboardService::DashboardService(Database& db) : db_(db), repo_(db) {}

DashboardData DashboardService::get_dashboard() const
{
    return db_.read([this](sqlite3* h) { return read_dashboard_data(h, repo_); });
}
