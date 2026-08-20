/**
 * @file
 * @brief C++ exception type wrapping libkatherine error codes.
 * @author Petr Mánek
 * @date 28.1.19
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdexcept>
#include <string>
#include <cstring>

#include <katherine/acquisition.h>

namespace katherine {

/**
 * @addtogroup cxx_api
 * @{
 */

class error: public std::runtime_error {
public:
    error(std::string what) : runtime_error{what} { }
};

class system_error: public error {
public:
    system_error(int rc) : error{std::strerror(rc)} { }
};

/** @} */

}
