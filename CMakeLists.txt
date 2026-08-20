# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.21)
# C++ is enabled by the cxx/ directory, so that KATHERINE_BUILD_CXX=OFF
# builds do not require a C++ compiler.
project(katherine VERSION 1.0.1 LANGUAGES C)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(KatherineFeatures)

# Oldest Doxygen able to process docs/Doxyfile (the file's format version).
set(KATHERINE_DOXYGEN_MINIMUM_VERSION 1.8.13)

# Oldest Python and Cython able to build the bindings in python/.
set(KATHERINE_PYTHON_MINIMUM_VERSION 3.5)
set(KATHERINE_CYTHON_MINIMUM_VERSION 3.0)

katherine_declare_feature(BUILD_CXX      "build C++ binaries"      DEFAULT ON  LEGACY BUILD_CXX)
katherine_declare_feature(BUILD_PYTHON   "build Python 3 bindings" DEFAULT OFF LEGACY BUILD_PYTHON AUTO_DETECTED)
katherine_declare_feature(BUILD_EXAMPLES "build example programs"  DEFAULT ON  LEGACY BUILD_EXAMPLES)
katherine_declare_feature(BUILD_DOXYGEN  "build API documentation" DEFAULT OFF AUTO_DETECTED)
katherine_feature_summary()

# The dialect this project's own binaries are built with (reproducible
# across compilers with different defaults); the language floors required
# by the libraries are declared on their targets with compile features.
set(CMAKE_C_STANDARD            17)
set(CMAKE_C_STANDARD_REQUIRED   ON)
set(CMAKE_CXX_STANDARD          17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Flatten build outputs into the build root for convenient in-tree use --
# but only in standalone builds: when consumed with add_subdirectory or
# FetchContent, the parent project's layout must be respected.
if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
endif()

include(GNUInstallDirs)

add_subdirectory(c)

if(KATHERINE_BUILD_CXX)
    add_subdirectory(cxx)
endif(KATHERINE_BUILD_CXX)

if(KATHERINE_BUILD_PYTHON)
    add_subdirectory(python)
endif(KATHERINE_BUILD_PYTHON)

# Added last: the documentation build consumes the file lists exported by
# the c/ and cxx/ directories.
if(KATHERINE_BUILD_DOXYGEN)
    add_subdirectory(docs)
endif(KATHERINE_BUILD_DOXYGEN)

# CMake package, so that consumers can find_package(katherine) and link
# katherine::katherine or katherine::katherinexx as an alternative to
# pkg-config. The katherine-targets export set is populated by the install
# rules in c/ and cxx/.
include(CMakePackageConfigHelpers)
set(KATHERINE_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/katherine")

install(EXPORT katherine-targets
        NAMESPACE katherine::
        DESTINATION "${KATHERINE_INSTALL_CMAKEDIR}")

configure_package_config_file(cmake/katherine-config.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/katherine-config.cmake"
        INSTALL_DESTINATION "${KATHERINE_INSTALL_CMAKEDIR}")
write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/katherine-config-version.cmake"
        COMPATIBILITY SameMajorVersion)
install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/katherine-config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/katherine-config-version.cmake"
        DESTINATION "${KATHERINE_INSTALL_CMAKEDIR}")
