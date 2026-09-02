/**
 * \file
 * \brief C++ wrapper for library version information.
 * \author Petr Mánek
 * \date 24.8.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>

#include <katherine/version.h>

namespace katherine {

/**
 * \addtogroup cxx_api
 * \{
 */

/**
 * Query the version of libkatherine loaded at runtime, composed the same
 * way as KATHERINE_VERSION_HEX.
 */
static inline std::uint64_t
version()
{
    return katherine_version();
}

/**
 * Query the version of libkatherine loaded at runtime, formatted the same
 * way as KATHERINE_VERSION_STRING.
 */
static inline const char *
version_string()
{
    return katherine_version_string();
}

/** \} */

}
