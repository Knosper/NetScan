#ifndef UTIL_PATH_UTILS_HPP
#define UTIL_PATH_UTILS_HPP

#include <string>

std::string join_path(const std::string& a, const std::string& b);

// Returns true if p is an absolute path (handles both Linux and Windows).
bool is_absolute_path(const std::string& p);

// Returns the directory component of file_path (without trailing separator).
std::string dir_of(const std::string& file_path);

// Resolves p relative to base_dir.  Absolute paths are returned unchanged.
std::string resolve_path(const std::string& base_dir, const std::string& p);

// Returns the absolute form of p.  If p is already absolute it is returned
// unchanged.  Otherwise it is resolved against the current working directory.
std::string to_absolute_path(const std::string& p);

bool path_exists(const std::string& path);
bool is_regular_file(const std::string& path);
bool is_directory(const std::string& path);
bool is_readable(const std::string& path);
bool is_writable(const std::string& path);
bool ensure_directory_exists(const std::string& path, std::string* error = nullptr);

#endif
