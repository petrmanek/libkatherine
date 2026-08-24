/**
 * @file
 * @brief Platform-specific definitions and symbol export macros.
 * @author Petr Mánek
 * @date 13.2.19
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>

/**
 * @addtogroup c_api
 * @{
 */

/* Useful macros courtesy of:
 * https://atomheartother.github.io/c++/2018/07/12/CPPDynLib.html
 */

// clang-format off
// Define KATHERINE_EXPORTED for any platform
#if defined _WIN32 || defined __CYGWIN__
  #ifdef katherine_EXPORTS
    // Exporting...
    #ifdef __GNUC__
      #define KATHERINE_EXPORTED __attribute__ ((dllexport))
    #else
      #define KATHERINE_EXPORTED __declspec(dllexport) // Note: actually gcc seems to also supports this syntax.
    #endif
  #else
    #ifdef __GNUC__
      #define KATHERINE_EXPORTED __attribute__ ((dllimport))
    #else
      #define KATHERINE_EXPORTED __declspec(dllimport) // Note: actually gcc seems to also supports this syntax.
    #endif
  #endif
  #define KATHERINE_NOT_EXPORTED
#else
  #if __GNUC__ >= 4
    #define KATHERINE_EXPORTED __attribute__ ((visibility ("default")))
    #define KATHERINE_NOT_EXPORTED  __attribute__ ((visibility ("hidden")))
  #else
    #define KATHERINE_EXPORTED
    #define KATHERINE_NOT_EXPORTED
  #endif
#endif

#ifdef _WIN32
#   define KATHERINE_WIN
#elif __APPLE__
    #include "TargetConditionals.h"
    #if TARGET_IPHONE_SIMULATOR
    #   error "iOS Simulator support not available"
    #elif TARGET_OS_IPHONE
    #   error "iOS support not available"
    #elif TARGET_OS_MAC
    #   define KATHERINE_NIX
    #else
    #   error "Unknown Apple platform"
    #endif
#elif __linux__
#   define KATHERINE_NIX
#elif __unix__
#   define KATHERINE_NIX
#elif defined(_POSIX_VERSION)
#   define KATHERINE_NIX
#else
#   error "Unknown platform"
#endif
// clang-format on

/**
 * Render a bool the way every katherine_*_snprint() function in this
 * library does for its bool fields: the literal token "true" or "false",
 * never a raw 0/1 or a translated word. Included from every public header
 * (this one), so it is always in scope where a snprint implementation or a
 * caller composing its own debug output needs it.
 * @param value Value to render
 * @return A statically allocated, NUL-terminated string. Never NULL.
 */
static inline const char *
katherine_str_bool(bool value)
{
    return value ? "true" : "false";
}

/** @} */
