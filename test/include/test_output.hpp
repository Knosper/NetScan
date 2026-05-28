#ifndef TEST_OUTPUT_HPP
#define TEST_OUTPUT_HPP

#include <cstdlib>
#include <iostream>
#include <string>

inline int finish_test(const std::string& test_name, bool ok)
{
    const char* green = "[32m";
    const char* red = "[31m";
    const char* reset = "[0m";

    if (ok)
        std::cout << green << test_name << ": success" << reset << "\n";
    else
        std::cerr << red << test_name << ": failed" << reset << "\n";

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

#endif
