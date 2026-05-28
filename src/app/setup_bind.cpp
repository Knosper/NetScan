#include "app/setup_server.hpp"

#include "app/config.hpp"
#include "util/path_utils.hpp"

namespace
{
static const int SETUP_SERVER_PORT = 8080;
static const char* SETUP_SERVER_HOST = "127.0.0.1";
}

SetupBind resolve_setup_bind(const std::string& absolute_config_path)
{
    SetupBind bind{SETUP_SERVER_HOST, SETUP_SERVER_PORT};
    if (!path_exists(absolute_config_path))
        return bind;

    const PersistedConfig persisted = load_persisted_config(absolute_config_path);
    if (!persisted.setup_pending)
        return bind;

    if (persisted.port >= 1 && persisted.port <= 65535)
        bind.port = persisted.port;
    return bind;
}
