#ifndef HTTP_API_HANDLER_HPP
#define HTTP_API_HANDLER_HPP

#include "platform.hpp"
#include "httplib/httplib.h"
#include "http/http_status.hpp"
#include "http/responses.hpp"
#include "util/logger.hpp"
#include <stdexcept>
#include <string>

struct ApiRequestContext
{
    const char* route;
    Logger&     logger;
};

template <typename Handler>
void handle_api_action(httplib::Response& res, const ApiRequestContext& request, Handler handler)
{
    try
    {
        handler();
    }
    catch (const std::exception& e)
    {
        request.logger.error(std::string(request.route) + " failed: " + e.what());
        set_json_error(res, JsonError{http_status::INTERNAL_SERVER_ERROR, "internal_error",
                                      "An unexpected error occurred"});
    }
}

// Executes handler logic inside a uniform try/catch block.
// On success the JSON string returned by handler() is sent with HTTP 200.
// On failure the exception is logged as "<route> failed: <what>" and a
// generic HTTP 500 JSON error response is returned.
template <typename Handler>
void handle_api_request(httplib::Response& res, const ApiRequestContext& request,
                        Handler handler)
{
    handle_api_action(res, request,
                      [&res, &handler]()
                      {
                          set_json_response(res, http_status::OK, handler());
                      });
}

#endif
