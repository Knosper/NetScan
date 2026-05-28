#ifndef PROCESS_SPEC_HPP
#define PROCESS_SPEC_HPP

#include <string>
#include <vector>

struct ProcessSpec
{
    std::string executable;
    std::vector<std::string> args;
};

#endif
