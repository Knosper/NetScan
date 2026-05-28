#include "http/static_files.hpp"
#include <fstream>
#include <sstream>

static const std::streamsize MAX_STATIC_FILE_BYTES = 16 * 1024 * 1024;

bool read_file(const std::string& path, std::string& content)
{
    std::ifstream file(path.c_str(), std::ios::binary | std::ios::ate);

    if (!file.is_open())
        return false;

    std::streamsize size = file.tellg();
    if (size < 0 || size > MAX_STATIC_FILE_BYTES)
        return false;

    file.seekg(0, std::ios::beg);

    std::stringstream buffer;
    buffer << file.rdbuf();
    content = buffer.str();
    return true;
}

void serve_static_file(httplib::Response& res, const std::string& path,
                       const std::string& content_type)
{
    std::string content;

    if (!read_file(path, content))
    {
        res.status = 404;
        res.set_content("File not found", "text/plain");
        return;
    }

    res.set_content(content, content_type);
}

// Returns the MIME type for the given file path based on its extension.
// Returns "application/octet-stream" for unrecognised extensions.
std::string get_mime_type(const std::string& path)
{
    std::string::size_type ext_pos = path.rfind('.');
    if (ext_pos == std::string::npos)
        return "application/octet-stream";

    std::string ext = path.substr(ext_pos);

    if (ext == ".html" || ext == ".htm") return "text/html";
    if (ext == ".css")                   return "text/css";
    if (ext == ".js")                    return "application/javascript";
    if (ext == ".json")                  return "application/json";
    if (ext == ".png")                   return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".gif")                   return "image/gif";
    if (ext == ".svg")                   return "image/svg+xml";
    if (ext == ".ico")                   return "image/x-icon";
    if (ext == ".txt")                   return "text/plain";
    if (ext == ".woff")                  return "font/woff";
    if (ext == ".woff2")                 return "font/woff2";
    if (ext == ".ttf")                   return "font/ttf";

    return "application/octet-stream";
}
