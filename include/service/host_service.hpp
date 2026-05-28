#ifndef SERVICE_HOST_SERVICE_HPP
#define SERVICE_HOST_SERVICE_HPP

#include "db/database.hpp"
#include "db/host_meta_repository.hpp"
#include "db/host_repository.hpp"
#include "db/scan_repository.hpp"
#include "host/host_types.hpp"
#include <memory>
#include <string>
#include <vector>

class HostService
{
public:
    explicit HostService(Database& db);

    std::vector<HostOverview> list_hosts(const HostListFilter& filter, int limit = 500, int offset = 0);
    int count_hosts(const HostListFilter& filter);
    std::unique_ptr<HostDetail> get_host_detail(const std::string& ip);
    void update_host_meta(const std::string& ip, const HostMeta& meta);
    void clear_host_meta_field(const std::string& ip, const std::string& field);
    // Prunes current host ports with persisted state == "closed".
    int prune_closed_ports();

private:
    Database&           db_;
    HostRepository      repo_;
    HostMetaRepository  meta_repo_;
    ScanRepository      scan_repo_;
};

#endif
