#pragma once

#include "db/database.hpp"
#include "db/scan_repository.hpp"
#include "service/topology_model.hpp"
#include <memory>

class TopologyService
{
public:
    TopologyService(Database& db, ScanRepository& repo);
    explicit TopologyService(Database& db);

    // Builds the topology graph for the given completed scan.
    // Returns NotFound if scan_id is unknown/deleted, NotReady if not completed.
    lsm::service::TopologyResult get_topology(int scan_id);

private:
    Database&                        db_;
    std::unique_ptr<ScanRepository>  owned_repo_;
    ScanRepository&                  repo_;
};
