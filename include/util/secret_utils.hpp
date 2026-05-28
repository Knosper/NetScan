#ifndef UTIL_SECRET_UTILS_HPP
#define UTIL_SECRET_UTILS_HPP

#include <string>

std::string generate_secret_key();
std::string hash_secret_key(const std::string& secret);
bool verify_secret_key(const std::string& provided, const std::string& stored_hash);
bool constant_time_equals(const std::string& left, const std::string& right);

#endif
