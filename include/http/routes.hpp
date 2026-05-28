#ifndef HTTP_ROUTES_HPP
#define HTTP_ROUTES_HPP

#include "app/auth_config.hpp"
#include "platform.hpp"
#include "httplib/httplib.h"
#include "app/app_context.hpp"
#include "service/dashboard_service.hpp"
#include "service/health_service.hpp"
#include "service/host_service.hpp"
#include "service/note_service.hpp"
#include "service/presence_service.hpp"
#include "service/scan_diff_service.hpp"
#include "service/scan_history_service.hpp"
#include "service/scan_service.hpp"
#include "service/settings_service.hpp"
#include "service/profile_service.hpp"
#include "service/scheduler_service.hpp"
#include "service/topology_service.hpp"

struct ApiServices
{
    DashboardService&   dashboard_svc;
    HealthService&      health_svc;
    HostService&        host_svc;
    NoteService&        note_svc;
    PresenceService&    presence_svc;
    ProfileService&     profile_svc;
    ScanDiffService&    scan_diff_svc;
    ScanHistoryService& scan_history_svc;
    ScanService&        scan_svc;
    SchedulerService&   scheduler_svc;
    SettingsService&    settings_svc;
    TopologyService&    topology_svc;
};

void register_routes(httplib::Server& svr, const AppContext& ctx, const AuthConfig& auth_config,
                     ApiServices& services);

#endif
