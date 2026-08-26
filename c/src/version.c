/**
 * @file
 * @brief Implementation of runtime version checking.
 * @author Petr Mánek
 * @date 20.8.26
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <katherine/version.h>

/**
 * Query the version of libkatherine loaded at runtime.
 * @return Version in the same composed form as `KATHERINE_VERSION_HEX`.
 */
uint64_t
katherine_version(void)
{
    return KATHERINE_VERSION_HEX;
}

/**
 * Query the version of libkatherine loaded at runtime.
 * @return Version string in the same form as `KATHERINE_VERSION_STRING`.
 */
const char *
katherine_version_string(void)
{
    return KATHERINE_VERSION_STRING;
}
