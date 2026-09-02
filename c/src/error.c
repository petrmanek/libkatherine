/**
 * \file
 * \brief Implementation of the library's own error domain.
 * \author Petr Mánek
 * \date 25.8.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <limits.h>
#include <katherine/error.h>

#ifndef DOXYGEN_SHOULD_SKIP_THIS

// Indexed directly by a katherine_error_t enumerator (entry 0, KATHERINE_E_OK,
// included so this stays a direct index rather than an off-by-one lookup).
#define KATHERINE_ERROR_COUNT (KATHERINE_E_SYSTEM + 1)

static const char *const KATHERINE_ERROR_STRINGS[KATHERINE_ERROR_COUNT] = {
    [KATHERINE_E_OK]          = "success",
    [KATHERINE_E_TIMEOUT]     = "operation timed out",
    [KATHERINE_E_IO]          = "transport i/o error",
    [KATHERINE_E_CLOSED]      = "session closed",
    [KATHERINE_E_ADDR]        = "address resolution or bind failed",
    [KATHERINE_E_BAD_CRD]     = "malformed command response",
    [KATHERINE_E_STRAY]       = "stray datagram rejected",
    [KATHERINE_E_PROTO]       = "protocol violation",
    [KATHERINE_E_UNSUPPORTED] = "not supported by this device",
    [KATHERINE_E_BAD_CHIP]    = "unexpected sensor chip identifier",
    [KATHERINE_E_STATE]       = "invalid in the current state",
    [KATHERINE_E_HW_UNKNOWN]  = "unrecognized hardware condition",
    [KATHERINE_E_INVAL]       = "invalid argument",
    [KATHERINE_E_NOMEM]       = "out of memory",
    [KATHERINE_E_SYSTEM]      = "system error",
};

#endif /* DOXYGEN_SHOULD_SKIP_THIS */

/**
 * Describe an error code.
 * \param error A katherine_error_t enumerator, which is what every function
 *   of this library returns, or its negation, which is what libkatherine 1.x
 *   returned.
 * \return A statically allocated, NUL-terminated, lowercase description.
 *   Never NULL: a value outside the domain renders as "unknown error".
 */
const char *
katherine_strerror(int error)
{
    // Negating INT_MIN overflows; no real caller ever passes it, but a
    // stray value must not become undefined behavior here.
    if (error == INT_MIN) return "unknown error";

    int idx = error < 0 ? -error : error;
    if (idx >= (int) KATHERINE_ERROR_COUNT) return "unknown error";

    return KATHERINE_ERROR_STRINGS[idx];
}
