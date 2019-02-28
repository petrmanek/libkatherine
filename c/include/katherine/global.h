/* Katherine Control Library
 *
 * This file was created on 13.2.19 by Petr Manek.
 * 
 * Contents of this file are copyrighted and subject to license
 * conditions specified in the LICENSE file located in the top
 * directory.
 */

#pragma once

/* Useful macros courtesy of:
 * https://atomheartother.github.io/c++/2018/07/12/CPPDynLib.html
 */

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
