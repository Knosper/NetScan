#include "http/api_topology.hpp"
#include "http/api_handler.hpp"
#include "http/http_status.hpp"
#include "http/responses.hpp"
#include "http/route_utils.hpp"

#include <nlohmann/json.hpp>
#include <stdexcept>

namespace
{
nlohmann::json::string_t topology_node_type_to_json_value(lsm::service::TopologyNodeType type)
{
    switch (type)
    {
    case lsm::service::TopologyNodeType::gateway:
        return "gateway";
    case lsm::service::TopologyNodeType::host:
        return "host";
    case lsm::service::TopologyNodeType::port:
        return "port";
    }

    throw std::logic_error("unknown topology node type");
}

nlohmann::json topology_node_to_json(const lsm::service::TopologyNode& node)
{
    nlohmann::json payload;
    payload["id"]    = node.id;
    payload["label"] = node.label;
    payload["meta"]  = node.meta;
    payload["type"]  = topology_node_type_to_json_value(node.type);
    return payload;
}

nlohmann::json topology_edge_to_json(const lsm::service::TopologyEdge& edge)
{
    nlohmann::json payload;
    payload["id"]     = edge.id;
    payload["source"] = edge.source;
    payload["target"] = edge.target;
    return payload;
}

nlohmann::json topology_to_json(const lsm::service::TopologyPayload& topology)
{
    nlohmann::json body;
    body["nodes"] = nlohmann::json::array();
    body["edges"] = nlohmann::json::array();

    for (const auto& node : topology.nodes)
        body["nodes"].push_back(topology_node_to_json(node));

    for (const auto& edge : topology.edges)
        body["edges"].push_back(topology_edge_to_json(edge));

    return body;
}

bool try_parse_topology_scan_id(const httplib::Request& req, httplib::Response& res, int& scan_id)
{
    if (try_parse_path_id(req, 1U, scan_id))
        return true;

    set_json_error(res,
                   JsonError{http_status::BAD_REQUEST, "bad_request", "invalid scan id"});
    return false;
}

bool handle_topology_success(const lsm::service::TopologyResult& result, httplib::Response& res)
{
    if (!result.ok())
        return false;
    if (!result.payload)
    {
        set_json_error(res, JsonError{http_status::INTERNAL_SERVER_ERROR, "internal_error",
                                      "topology result did not contain a payload"});
        return true;
    }

    set_json_response(res, http_status::OK, topology_to_json(*result.payload).dump());
    return true;
}

void handle_topology_error(lsm::service::TopologyError error, httplib::Response& res)
{
    switch (error)
    {
    case lsm::service::TopologyError::NotFound:
        set_json_error(res, JsonError{http_status::NOT_FOUND, "not_found", "scan not found"});
        return;
    case lsm::service::TopologyError::NotReady:
        set_json_error(res, JsonError{http_status::CONFLICT, "conflict",
                                      "only completed scans can be loaded as topology"});
        return;
    default:
        set_json_error(res, JsonError{http_status::INTERNAL_SERVER_ERROR, "internal_error",
                                      "failed to build topology"});
        return;
    }
}

void handle_topology_request(const httplib::Request& req, httplib::Response& res,
                             TopologyService& service, Logger& logger)
{
    handle_api_action(
        res, ApiRequestContext{"GET /api/scans/:id/topology", logger},
        [&req, &res, &service]()
        {
            int scan_id = 0;
            if (!try_parse_topology_scan_id(req, res, scan_id))
                return;

            const lsm::service::TopologyResult result = service.get_topology(scan_id);
            if (handle_topology_success(result, res))
                return;

            handle_topology_error(result.error, res);
        });
}
} // namespace

void register_topology_routes(httplib::Server& svr, TopologyService& service, Logger& logger)
{
    TopologyService* svc = &service;
    Logger*          log = &logger;

    svr.Get(R"(/api/scans/(\d+)/topology)",
            [svc, log](const httplib::Request& req, httplib::Response& res)
            { handle_topology_request(req, res, *svc, *log); });
}
