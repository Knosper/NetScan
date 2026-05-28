#include "http/api_static.hpp"
#include "http/http_status.hpp"
#include "http/responses.hpp"
#include "http/static_files.hpp"
#include "util/path_utils.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <limits.h>
#include <stdlib.h>
#endif
#include <cctype>

// Resolve a path to its canonical, absolute form.
// Returns an empty string on failure (path does not exist or syscall error).
static std::string canonical_path(const std::string& path)
{
#ifdef _WIN32
    HANDLE handle = CreateFileA(path.c_str(), 0,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return "";
    char buf[4096];
    DWORD len = GetFinalPathNameByHandleA(handle, buf, static_cast<DWORD>(sizeof(buf)),
                                          FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    CloseHandle(handle);
    if (len == 0 || len >= sizeof(buf))
        return "";
    std::string resolved(buf, len);
    const std::string unc_prefix = "\\\\?\\UNC\\";
    const std::string dos_prefix = "\\\\?\\";
    if (resolved.rfind(unc_prefix, 0) == 0)
        resolved = "\\\\" + resolved.substr(unc_prefix.size());
    else if (resolved.rfind(dos_prefix, 0) == 0)
        resolved = resolved.substr(dos_prefix.size());
    return resolved;
#else
    char buf[PATH_MAX];
    if (realpath(path.c_str(), buf) == nullptr)
        return "";
    return std::string(buf);
#endif
}

static void normalise_for_containment(std::string& path)
{
    for (std::string::size_type i = 0; i < path.size(); ++i)
    {
        if (path[i] == '\\')
            path[i] = '/';
#ifdef _WIN32
        path[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(path[i])));
#endif
    }
}

static std::string lexical_normalise_candidate(const std::string& candidate)
{
    std::string normalized_candidate = candidate;
    normalise_for_containment(normalized_candidate);
    return normalized_candidate;
}


static std::string normalise_separators(const std::string& path)
{
    std::string result = path;
    for (std::string::size_type i = 0; i < result.size(); ++i)
    {
        if (result[i] == '\\')
            result[i] = '/';
    }
    return result;
}

static bool has_path_traversal(const std::string& path)
{
    if (path == "..")
        return true;
    if (path.size() >= 3 && path.rfind("../", 0) == 0)
        return true;
    if (path.find("/../") != std::string::npos)
        return true;
    if (path.size() >= 3 && path.compare(path.size() - 3, 3, "/..") == 0)
        return true;
    return false;
}

static bool is_api_path(const std::string& path)
{
    return path == "api" || (path.size() >= 4 && path.compare(0, 4, "api/") == 0);
}

static bool reject_api_request(const std::string& path, httplib::Response& res)
{
    if (!is_api_path(path))
        return false;

    set_json_error(res, JsonError{http_status::NOT_FOUND, "not_found", "API endpoint not found"});
    return true;
}

static bool reject_path_traversal(const std::string& path, httplib::Response& res)
{
    if (!has_path_traversal(path))
        return false;

    res.status = http_status::BAD_REQUEST;
    res.set_content("Bad Request", "text/plain");
    return true;
}

static bool reject_absolute_path(const std::string& path, httplib::Response& res)
{
    bool absolute = is_absolute_path(path);
#ifdef _WIN32
    // Also block paths containing ':' which could form drive prefixes after decoding.
    if (!absolute)
        absolute = path.find(':') != std::string::npos;
#endif
    if (!absolute)
        return false;

    res.status = http_status::BAD_REQUEST;
    res.set_content("Bad Request", "text/plain");
    return true;
}

static std::string lexical_resolve_candidate(const std::string& candidate)
{
    const bool absolute = !candidate.empty() && candidate[0] == '/';
    std::string norm;
    norm.reserve(candidate.size());
    if (absolute)
        norm = "/";

    for (std::string::size_type i = 0; i < candidate.size(); )
    {
        std::string::size_type sep = candidate.find('/', i);
        if (sep == std::string::npos)
            sep = candidate.size();
        std::string seg = candidate.substr(i, sep - i);
        i = sep + 1;
        if (seg.empty() || seg == ".")
            continue;
        if (seg == "..")
        {
            if (norm == "/" || norm.empty())
                continue;
            std::string::size_type last = norm.rfind('/');
            if (last == std::string::npos)
                norm = "";
            else if (last == 0)
                norm = "/";
            else
                norm = norm.substr(0, last);
            continue;
        }

        if (norm.empty() || norm == "/")
            norm += seg;
        else
            norm += '/' + seg;
    }

    return norm;
}

static bool has_root_prefix(const std::string& root, const std::string& candidate)
{
    if (candidate.size() < root.size())
        return false;
    if (candidate.substr(0, root.size()) != root)
        return false;
    if (candidate.size() > root.size() && candidate[root.size()] != '/' &&
        candidate[root.size()] != '\\')
        return false;
    return true;
}

// Lexical containment check: resolves ".." segments without requiring the path
// to exist on disk. Returns false when candidate escapes web_dir.
static bool lexical_within_web_root(const std::string& web_dir, const std::string& candidate)
{
    std::string root = canonical_path(web_dir);
    if (root.empty())
        return false;
    normalise_for_containment(root);

    const std::string normalized_candidate = lexical_normalise_candidate(candidate);
    const std::string resolved_candidate = lexical_resolve_candidate(normalized_candidate);
    return has_root_prefix(root, resolved_candidate);
}

static void serve_index(const AppContext& ctx, httplib::Response& res)
{
    serve_static_file(res, join_path(ctx.config.web_dir, "index.html"), "text/html");
}

static void serve_static_request(const AppContext& ctx, const httplib::Request& req,
                                 httplib::Response& res)
{
    std::string rel_path = normalise_separators(req.matches[1].str());
    if (reject_api_request(rel_path, res) || reject_path_traversal(rel_path, res) ||
        reject_absolute_path(rel_path, res))
        return;

    std::string joined = join_path(ctx.config.web_dir, rel_path);
    if (!lexical_within_web_root(ctx.config.web_dir, joined))
    {
        res.status = http_status::FORBIDDEN;
        res.set_content("Forbidden", "text/plain");
        return;
    }

    if (!path_exists(joined))
    {
        res.status = http_status::NOT_FOUND;
        res.set_content("Not Found", "text/plain");
        return;
    }

    serve_static_file(res, joined, get_mime_type(rel_path));
}

void register_static_routes(httplib::Server& svr, const AppContext& ctx)
{
    svr.Get("/",
            [&ctx](const httplib::Request&, httplib::Response& res) { serve_index(ctx, res); });
    svr.Get("/(.+)",
            [&ctx](const httplib::Request& req, httplib::Response& res)
            { serve_static_request(ctx, req, res); });
}
