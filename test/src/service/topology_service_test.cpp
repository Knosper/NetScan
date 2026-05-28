#include "db/database.hpp"
#include "db/schema.hpp"
#include "db/scan_repository.hpp"
#include "scan_service_test_support.hpp"
#include "service/topology_service.hpp"
#include "test_output.hpp"
#include "util/logger.hpp"

#include <cstdio>
#include <memory>
#include <string>

namespace
{
int count_nodes_of_type(const lsm::service::TopologyPayload& payload,
                        lsm::service::TopologyNodeType type)
{
    int count = 0;
    for (const auto& node : payload.nodes)
    {
        if (node.type == type)
            ++count;
    }
    return count;
}

bool has_port_node(const lsm::service::TopologyPayload& payload, const std::string& ip, int port)
{
    for (const auto& node : payload.nodes)
    {
        if (node.type != lsm::service::TopologyNodeType::port)
            continue;
        auto ip_it   = node.meta.find("ip");
        auto port_it = node.meta.find("port");
        if (ip_it != node.meta.end() && ip_it->second == ip &&
            port_it != node.meta.end() && port_it->second == std::to_string(port))
        {
            return true;
        }
    }
    return false;
}

bool has_host_node(const lsm::service::TopologyPayload& payload, const std::string& ip)
{
    for (const auto& node : payload.nodes)
    {
        if (node.type == lsm::service::TopologyNodeType::host &&
            node.meta.find("ip") != node.meta.end() && node.meta.at("ip") == ip)
        {
            return true;
        }
    }
    return false;
}

const lsm::service::TopologyNode* find_gateway(const lsm::service::TopologyPayload& payload)
{
    for (const auto& node : payload.nodes)
    {
        if (node.type == lsm::service::TopologyNodeType::gateway)
            return &node;
    }
    return nullptr;
}
} // namespace

int main()
{
    bool all_ok = true;

    const std::string db_path = make_temp_db_path("/tmp/netscan-topology-service-test-XXXXXX.db");
    all_ok = expect(!db_path.empty(), "temporary database path should be created") && all_ok;

    if (!db_path.empty())
    {
        Database db(db_path);
        Logger logger;
        configure_database_runtime(db, logger);
        init_schema(db, logger);

        ScanRepository repo(db);
        TopologyService topology_service(db, repo);

        // Single scan covering all relevant host shapes:
        //  - 127.0.0.1: no ports at all (only present from discovery)
        //  - 127.0.0.2: one open port (should be the only host node rendered)
        //  - 127.0.0.3: only closed ports (should be hidden)
        const int scan_id = persist_scan(
            db, repo,
            {{"127.0.0.0/29", "", false, ""}, PersistedScanState::Completed, false,
             lsm::scan::ScanSnapshot{{
                 {"127.0.0.1", "localhost", {}},
                 {"127.0.0.2", "",
                     {{"tcp", 22, "open", "ssh"},
                      {"tcp", 25, "closed", "smtp"}}},
                 {"127.0.0.3", "", {{"tcp", 80, "closed", "http"}}}
             }}});
        all_ok = expect(scan_id > 0, "mixed scan should persist") && all_ok;

        lsm::service::TopologyResult result = topology_service.get_topology(scan_id);
        all_ok = expect(result.ok(), "topology should be buildable") && all_ok;
        all_ok = expect(result.payload != nullptr, "topology should return a payload") && all_ok;

        if (result.payload)
        {
            const lsm::service::TopologyNode* gateway = find_gateway(*result.payload);
            all_ok = expect(gateway != nullptr, "topology should include a local root node") &&
                     all_ok;
            if (gateway)
            {
                all_ok = expect(gateway->label == "NetScan",
                                "local root node should not use the scan target as label") &&
                         all_ok;
                all_ok = expect(gateway->meta.find("scan_target") != gateway->meta.end() &&
                                    gateway->meta.at("scan_target") == "127.0.0.0/29",
                                "local root node should preserve the selected scan target in meta") &&
                         all_ok;
            }

            all_ok = expect(count_nodes_of_type(*result.payload,
                                                lsm::service::TopologyNodeType::gateway) == 1,
                            "topology should have exactly one local root node") &&
                     all_ok;
            all_ok = expect(!has_host_node(*result.payload, "127.0.0.1"),
                            "topology should hide hosts without any ports") &&
                     all_ok;
            all_ok = expect(has_host_node(*result.payload, "127.0.0.2"),
                            "topology should include hosts with open ports") &&
                     all_ok;
            all_ok = expect(!has_host_node(*result.payload, "127.0.0.3"),
                            "topology should hide hosts that only have closed ports") &&
                     all_ok;
            all_ok = expect(count_nodes_of_type(*result.payload,
                                                lsm::service::TopologyNodeType::host) == 1,
                            "topology should expose only one host node") &&
                     all_ok;
            all_ok = expect(count_nodes_of_type(*result.payload,
                                                lsm::service::TopologyNodeType::port) == 1,
                            "topology should expose only the open port node") &&
                     all_ok;
        }

        // Host-discovery-only scan: every responding host must be visible,
        // even when there is no port information.
        const int discovery_scan_id = persist_scan(
            db, repo,
            {{"127.0.0.0/30", "", true, ""}, PersistedScanState::Completed, true,
             lsm::scan::ScanSnapshot{{
                 {"127.0.0.10", "", {}},
                 {"127.0.0.11", "", {}}
             }}});
        all_ok = expect(discovery_scan_id > 0, "host-discovery scan should persist") && all_ok;

        lsm::service::TopologyResult discovery_result =
            topology_service.get_topology(discovery_scan_id);
        all_ok = expect(discovery_result.ok(),
                        "host-discovery topology should be buildable") &&
                 all_ok;
        if (discovery_result.payload)
        {
            all_ok = expect(has_host_node(*discovery_result.payload, "127.0.0.10"),
                            "host-discovery topology should include first responding host") &&
                     all_ok;
            all_ok = expect(has_host_node(*discovery_result.payload, "127.0.0.11"),
                            "host-discovery topology should include second responding host") &&
                     all_ok;
            all_ok = expect(count_nodes_of_type(*discovery_result.payload,
                                                lsm::service::TopologyNodeType::port) == 0,
                            "host-discovery topology should not expose any port nodes") &&
                     all_ok;
        }

        // Cross-scan isolation: get_topology(scan_id) must not pull hosts
        // from a different scan into the rendered graph.
        lsm::service::TopologyResult first_result = topology_service.get_topology(scan_id);
        if (first_result.payload)
        {
            all_ok = expect(!has_host_node(*first_result.payload, "127.0.0.10"),
                            "topology should not leak hosts from other scans") &&
                     all_ok;
        }

        // Unknown scan id: caller should get a clean NotFound, not a payload.
        lsm::service::TopologyResult missing_result = topology_service.get_topology(999999);
        all_ok = expect(!missing_result.ok() && missing_result.payload == nullptr,
                        "unknown scan id should not produce a payload") &&
                 all_ok;

        // LSM-063: Active topology ports derived from provable current port state.
        //
        // Scenario:
        //   Step 1 — full-range scan: host 127.0.0.5 has 8080 and 17877 open.
        //   Step 2 — full-range scan: same host, only 8080 open.
        //            17877 is covered and absent → evidence of closure.
        //            Topology for step-1 scan must no longer show 17877.
        //   Step 3 — targeted scan covering only port 8080.
        //            17877 is not in the covered range → evidence remains from step 2.
        //            (Topology for step-1 scan should still not show 17877.)
        //
        // Fallback scenario (separate DB state):
        //   Scan A + Scan B: scan A has both ports open; scan B covers only 8080 (not 17877).
        //   17877 is never disproven.  Topology for scan A must still show 17877.

        const std::string coverage_all_ports = "";     // empty = full range (derive_scan_port_coverage)
        const std::string coverage_port_8080  = "8080"; // targeted: covers only 8080

        // Step 1: host with 8080 + 17877 open, full coverage.
        const int tc063_scan1 = persist_scan(
            db, repo,
            {{"127.0.0.5", coverage_all_ports, false, ""}, PersistedScanState::Completed, false,
             lsm::scan::ScanSnapshot{{
                 {"127.0.0.5", "",
                     {{"tcp", 8080, "open", "http-alt"},
                      {"tcp", 17877, "open", "unknown"}}}
             }}});
        all_ok = expect(tc063_scan1 > 0, "tc063: step-1 scan should persist") && all_ok;

        // Step 2: same host, same full coverage, only 8080 open → 17877 closed.
        const int tc063_scan2 = persist_scan(
            db, repo,
            {{"127.0.0.5", coverage_all_ports, false, ""}, PersistedScanState::Completed, false,
             lsm::scan::ScanSnapshot{{
                 {"127.0.0.5", "",
                     {{"tcp", 8080, "open", "http-alt"}}}
             }}});
        all_ok = expect(tc063_scan2 > 0, "tc063: step-2 scan should persist") && all_ok;

        // After step 2: topology for step-1 scan must no longer expose 17877.
        lsm::service::TopologyResult after_step2 = topology_service.get_topology(tc063_scan1);
        all_ok = expect(after_step2.ok(), "tc063: topology after step-2 should be buildable") &&
                 all_ok;
        if (after_step2.payload)
        {
            all_ok = expect(has_port_node(*after_step2.payload, "127.0.0.5", 8080),
                            "tc063: port 8080 should remain visible after step-2") &&
                     all_ok;
            all_ok = expect(!has_port_node(*after_step2.payload, "127.0.0.5", 17877),
                            "tc063: port 17877 must be removed after covering scan closed it") &&
                     all_ok;
        }

        // Step 3: targeted scan covering only port 8080.  17877 remains disproven from step 2.
        const int tc063_scan3 = persist_scan(
            db, repo,
            {{"127.0.0.5", coverage_port_8080, false, ""}, PersistedScanState::Completed, false,
             lsm::scan::ScanSnapshot{{
                 {"127.0.0.5", "",
                     {{"tcp", 8080, "open", "http-alt"}}}
             }}});
        all_ok = expect(tc063_scan3 > 0, "tc063: step-3 scan should persist") && all_ok;

        lsm::service::TopologyResult after_step3 = topology_service.get_topology(tc063_scan1);
        all_ok = expect(after_step3.ok(), "tc063: topology after step-3 should be buildable") &&
                 all_ok;
        if (after_step3.payload)
        {
            all_ok = expect(!has_port_node(*after_step3.payload, "127.0.0.5", 17877),
                            "tc063: port 17877 must stay removed after non-covering step-3 scan") &&
                     all_ok;
        }

        // Fallback scenario: targeted scan (non-covering) must not disprove a port.
        const int tc063_scanA = persist_scan(
            db, repo,
            {{"127.0.0.6", coverage_all_ports, false, ""}, PersistedScanState::Completed, false,
             lsm::scan::ScanSnapshot{{
                 {"127.0.0.6", "",
                     {{"tcp", 8080, "open", "http-alt"},
                      {"tcp", 17877, "open", "unknown"}}}
             }}});
        all_ok = expect(tc063_scanA > 0, "tc063: fallback scan-A should persist") && all_ok;

        // Scan B covers only 8080 — 17877 is not in coverage, not disproven.
        const int tc063_scanB = persist_scan(
            db, repo,
            {{"127.0.0.6", coverage_port_8080, false, ""}, PersistedScanState::Completed, false,
             lsm::scan::ScanSnapshot{{
                 {"127.0.0.6", "",
                     {{"tcp", 8080, "open", "http-alt"}}}
             }}});
        all_ok = expect(tc063_scanB > 0, "tc063: fallback scan-B should persist") && all_ok;

        lsm::service::TopologyResult fallback_result = topology_service.get_topology(tc063_scanA);
        all_ok = expect(fallback_result.ok(),
                        "tc063: fallback topology should be buildable") &&
                 all_ok;
        if (fallback_result.payload)
        {
            all_ok = expect(has_port_node(*fallback_result.payload, "127.0.0.6", 8080),
                            "tc063: fallback port 8080 should remain visible") &&
                     all_ok;
            all_ok = expect(has_port_node(*fallback_result.payload, "127.0.0.6", 17877),
                            "tc063: port 17877 must remain visible when not covered by later scan") &&
                     all_ok;
        }
    }

    if (!db_path.empty())
        std::remove(db_path.c_str());

    return finish_test("topology_service_test", all_ok);
}
