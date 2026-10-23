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

/**
 * Base of every exception this wrapper throws.
 *
 * A caller that does not care which failure it was catches this; one that
 * does catches katherine::system_error and reads its code.
 */
class error: public std::runtime_error {
public:
    /// \param what Message std::runtime_error::what() returns.
    error(std::string what) : runtime_error{what} { }
};

/// A failure the library reported, carrying the code it reported.
class system_error: public error {
public:
    /// \param rc Code the library returned, which what() is rendered from.
    system_error(katherine_error_t rc) : error{katherine_strerror(rc)}, code_{rc} { }

    /**
     * The code this was raised from.
     *
     * What std::runtime_error::what() renders is katherine_strerror()'s
     * description of this code. A caller that wants to branch on the failure
     * rather than print it needs the code itself, which is what this is for.
     */
    katherine_error_t
    code() const noexcept
    {
        return code_;
    }

private:
    katherine_error_t code_;
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
