/**
 * \file
 * \brief C++ exception type wrapping libkatherine error codes.
 * \author Petr Mánek
 * \date 28.1.19
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdexcept>
#include <string>

#include <katherine/error.h>

namespace katherine {

/**
 * \addtogroup katherine_cxx_api
 * \{
 */

class error: public std::runtime_error {
public:
    error(std::string what) : runtime_error{what} { }
};

class system_error: public error {
public:
    system_error(int rc) : error{katherine_strerror(rc)} { }
};

/** \} */

namespace detail {

/**
 * A katherine::system_error for a failed return code, and nothing for
 * KATHERINE_E_OK.
 *
 * Every wrapper in this library calls exactly one C function and has nothing
 * to add to its failure, so the call and the throw are one statement rather
 * than a four-line block repeated at each of them. A wrapper that does have
 * something to add still writes the block out by hand.
 */
inline void
check(katherine_error_t res)
{
    if (res != KATHERINE_E_OK) {
        throw katherine::system_error{res};
    }
}

}

}
