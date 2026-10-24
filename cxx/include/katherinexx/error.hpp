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
 * The exception this wrapper throws, carrying the code it was raised from.
 *
 * One class rather than two. There used to be a base with no code() and a
 * derived system_error with one, but nothing ever threw the base, so the two
 * selected the same set and the only thing the split achieved was that
 * `catch (const katherine::error& e)` followed by `e.code()` -- the natural
 * thing to write, and what the README wrote -- did not compile.
 *
 * The name std::system_error was wrong for it besides: that means an
 * operating-system error, and KATHERINE_E_STATE is not one. It also makes
 * C++ read like Python, where katherine.Error itself carries .code and there
 * is no second class.
 */
class error: public std::runtime_error {
public:
    /// \param rc Code the library returned, which what() is rendered from.
    error(katherine_error_t rc) : runtime_error{katherine_strerror(rc)}, code_{rc} { }

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

/**
 * \deprecated The old name for katherine::error, kept so that source written
 *   against the two-class shape still compiles. Removed in 3.0.
 */
using system_error = error;

/** \} */

namespace detail {

/**
 * A katherine::error for a failed return code, and nothing for
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
        throw katherine::error{res};
    }
}

}

}
