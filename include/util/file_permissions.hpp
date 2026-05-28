#ifndef UTIL_FILE_PERMISSIONS_HPP
#define UTIL_FILE_PERMISSIONS_HPP

#include <string>

namespace util
{
bool restrict_file_to_owner(const std::string& path, std::string* error = nullptr);
}

#endif
