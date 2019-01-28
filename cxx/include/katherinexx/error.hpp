#pragma once

#include <stdexcept>
#include <string>
#include <cstring>

#include <katherine/acquisition.h>

namespace katherine {

class error: public std::runtime_error {
public:
    error(std::string what) :runtime_error{what} { }

};

class system_error: public error {
public:
    system_error(int rc) :error{std::strerror(rc)} { }

};

}
