#include "app/service_wiring.hpp"

ServicesBundle::ServicesBundle(const ServicesBundleDeps& deps)
    : dashboard_service(deps.db),
          health_service(deps.db, deps.logger, deps.config.host),
          host_service(deps.db),
          note_service(deps.db, deps.logger),
          settings_service(deps.db, deps.config_path, deps.logger),
          scan_service(deps.db, deps.logger, &settings_service),
          presence_service(deps.db, deps.logger, &scan_service),
          scan_repo(deps.db),
          scan_diff_service(deps.db, scan_repo),
          scan_history_service(deps.db, scan_repo),
          profile_service(deps.db, scan_service),
          scheduler_service(deps.db, scan_service, deps.logger),
          presence_scheduler_service(presence_service, deps.logger),
          topology_service(deps.db, scan_repo),
          api_services{dashboard_service, health_service, host_service, note_service,
                       presence_service, profile_service, scan_diff_service,
                       scan_history_service, scan_service, scheduler_service,
                       settings_service, topology_service}
    {
        scan_service.set_nmap_path(deps.config.nmap_path);
    }
