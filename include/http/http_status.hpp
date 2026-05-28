#ifndef HTTP_HTTP_STATUS_HPP
#define HTTP_HTTP_STATUS_HPP

namespace http_status
{
    constexpr int OK = 200;
    constexpr int CREATED = 201;
    constexpr int ACCEPTED = 202;
    constexpr int NO_CONTENT = 204;
    constexpr int BAD_REQUEST = 400;
    constexpr int UNAUTHORIZED = 401;
    constexpr int FORBIDDEN = 403;
    constexpr int NOT_FOUND = 404;
    constexpr int METHOD_NOT_ALLOWED = 405;
    constexpr int CONFLICT = 409;
    constexpr int PAYLOAD_TOO_LARGE = 413;
    constexpr int UNSUPPORTED_MEDIA_TYPE = 415;
    constexpr int TOO_MANY_REQUESTS = 429;
    constexpr int SERVICE_UNAVAILABLE = 503;
    constexpr int INTERNAL_SERVER_ERROR = 500;
} // namespace http_status

#endif
