#ifndef COMMAND_HPP
#define COMMAND_HPP

#include <string>
#include <vector>

struct Command 
{
    std::string              name;
    std::vector<std::string> params;
};

#endif
