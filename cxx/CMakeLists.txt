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
    "include/katherinexx/gpio.hpp"
    "include/katherinexx/katherinexx.hpp"
    "include/katherinexx/pulse.hpp"
    "include/katherinexx/px_config.hpp"
    "include/katherinexx/repr.hpp"
    "include/katherinexx/tdc.hpp"
    "include/katherinexx/toa.hpp"
    "include/katherinexx/version.hpp"
)

# The project-wide documentation build in docs/ documents these files.
set(KATHERINEXX_DOCUMENTED_FILES ${KATHERINEXX_HEADERS})
list(TRANSFORM KATHERINEXX_DOCUMENTED_FILES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
set(KATHERINEXX_DOCUMENTED_FILES "${KATHERINEXX_DOCUMENTED_FILES}" PARENT_SCOPE)

add_library(katherinexx INTERFACE)
add_library(katherine::katherinexx ALIAS katherinexx)
# The headers are the product: the C++11 floor is a usage requirement that
# propagates to consumers and is recorded in the installed CMake package.
target_compile_features(katherinexx INTERFACE cxx_std_11)
target_link_libraries(katherinexx INTERFACE katherine)
target_include_directories(katherinexx
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# The same assertion c/CMakeLists.txt makes about the C headers, for the
# ones installed here: a consumer may include any single header first, and
# nothing in this tree would notice one that had come to depend on another
# arriving before it. Compiled at the interface's own C++11 floor, since a
# consumer held to that floor is the one a missing include breaks first.
if(KATHERINE_BUILD_TESTS)
    set(KATHERINEXX_STANDALONE_TUS "")
    foreach(hdr IN LISTS KATHERINEXX_HEADERS)
        get_filename_component(stem "${hdr}" NAME_WE)
        set(tu "${CMAKE_CURRENT_BINARY_DIR}/standalone/${stem}.cpp")
        file(WRITE "${tu}" "#include <katherinexx/${stem}.hpp>\n")
        list(APPEND KATHERINEXX_STANDALONE_TUS "${tu}")
    endforeach()
    add_library(katherinexx_headers_standalone OBJECT ${KATHERINEXX_STANDALONE_TUS})
    target_link_libraries(katherinexx_headers_standalone PRIVATE katherinexx)
    target_compile_features(katherinexx_headers_standalone PRIVATE cxx_std_11)
endif()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/katherinexx.pc.in katherinexx.pc @ONLY)

install(TARGETS katherinexx EXPORT katherine-targets)
install(FILES ${KATHERINEXX_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/katherinexx)
# Header-only, but kept next to katherine.pc for consistency.
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/katherinexx.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

if(KATHERINE_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(KATHERINE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
