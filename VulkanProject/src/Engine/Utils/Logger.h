#pragma once

#include <string>

class Logger
{
public:
    static void Info(const std::string& msg);
    static void Error(const std::string& msg);
    // etc.
};
