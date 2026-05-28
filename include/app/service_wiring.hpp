#ifndef APP_SERVICE_WIRING_HPP
#define APP_SERVICE_WIRING_HPP

#include "app/config.hpp"
#include "db/database.hpp"
#include "db/scan_repository.hpp"
#include "http/routes.hpp"
#include "service/dashboard_service.hpp"
#include "service/health_service.hpp"
#include "service/host_service.hpp"
#include "service/note_service.hpp"
#include "service/presence_scheduler_service.hpp"
#include "service/presence_service.hpp"
#include "service/profile_service.hpp"
#include "service/scheduler_service.hpp"
#include "service/scan_diff_service.hpp"
#include "service/scan_history_service.hpp"
#include "service/scan_service.hpp"
#include "service/settings_service.hpp"
#include "service/topology_service.hpp"
#include "util/logger.hpp"
#include <string>

struct ServicesBundleDeps
{
    Database& db;
    const AppConfig& config;
    const std::string& config_path;
    Logger& logger;
};

struct ServicesBundle
{
    DashboardService dashboard_service;
    HealthService health_service;
    HostService host_service;
    NoteService note_service;
    SettingsService settings_service;
    ScanService scan_service;
    PresenceService presence_service;
    ScanRepository scan_repo;
    ScanDiffService scan_diff_service;
    ScanHistoryService scan_history_service;
    ProfileService profile_service;
    SchedulerService scheduler_service;
    PresenceSchedulerService presence_scheduler_service;
    TopologyService topology_service;
    ApiServices api_services;

    explicit ServicesBundle(const ServicesBundleDeps& deps);
};

#endif
