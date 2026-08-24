# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

enable_language(CXX)

set(KATHERINEXX_HEADERS
    "include/katherinexx/acquisition.hpp"
    "include/katherinexx/config.hpp"
    "include/katherinexx/device.hpp"
    "include/katherinexx/error.hpp"
    "include/katherinexx/katherinexx.hpp"
    "include/katherinexx/px_config.hpp"
    "include/katherinexx/udp.hpp"
    "include/katherinexx/version.hpp"
)

# The project-wide documentation build in docs/ documents these files.
set(KATHERINEXX_DOCUMENTED_FILES ${KATHERINEXX_HEADERS})
list(TRANSFORM KATHERINEXX_DOCUMENTED_FILES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
set(KATHERINEXX_DOCUMENTED_FILES "${KATHERINEXX_DOCUMENTED_FILES}" PARENT_SCOPE)

add_library(katherinexx INTERFACE)
add_library(katherine::katherinexx ALIAS katherinexx)
# The headers are the product: the C++14 floor is a usage requirement that
# propagates to consumers and is recorded in the installed CMake package.
target_compile_features(katherinexx INTERFACE cxx_std_14)
target_link_libraries(katherinexx INTERFACE katherine)
target_include_directories(katherinexx
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/katherinexx.pc.in katherinexx.pc @ONLY)

install(TARGETS katherinexx EXPORT katherine-targets)
install(FILES ${KATHERINEXX_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/katherinexx)
# Header-only, but kept next to katherine.pc for consistency.
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/katherinexx.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

if(KATHERINE_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
