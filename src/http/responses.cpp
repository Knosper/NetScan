#include "http/responses.hpp"
#include <nlohmann/json.hpp>

void set_json_response(httplib::Response& res, int status, const std::string& body)
{
    res.status = status;
    res.set_content(body, "application/json");
}

void set_json_error(httplib::Response& res, const JsonError& error)
{
    nlohmann::json body;
    body["status"] = "error";
    body["type"] = error.type;
    body["message"] = error.message;
    set_json_response(res, error.status, body.dump());
}
