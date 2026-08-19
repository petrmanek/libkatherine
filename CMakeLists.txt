# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.21)
project(katherine VERSION 1.0.1 LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(KatherineFeatures)

# Oldest Doxygen able to process c/Doxyfile (the file's format version).
set(KATHERINE_DOXYGEN_MINIMUM_VERSION 1.8.13)

katherine_declare_feature(BUILD_CXX      "build C++ binaries"      DEFAULT ON  LEGACY BUILD_CXX)
katherine_declare_feature(BUILD_PYTHON   "build Python 3 bindings" DEFAULT OFF LEGACY BUILD_PYTHON)
katherine_declare_feature(BUILD_EXAMPLES "build example programs"  DEFAULT ON  LEGACY BUILD_EXAMPLES)
katherine_declare_feature(BUILD_DOXYGEN  "build API documentation" DEFAULT OFF AUTO_DETECTED)
katherine_feature_summary()

set(CMAKE_C_STANDARD    17)
set(CMAKE_CXX_STANDARD  17)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

include(GNUInstallDirs)

add_subdirectory(c)

if(KATHERINE_BUILD_CXX)
    add_subdirectory(cxx)
endif(KATHERINE_BUILD_CXX)

if(KATHERINE_BUILD_PYTHON)
    add_subdirectory(python)
endif(KATHERINE_BUILD_PYTHON)
