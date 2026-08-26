# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.21)
# C++ is enabled by the cxx/ directory, so that KATHERINE_BUILD_CXX=OFF
# builds do not require a C++ compiler.
project(katherine VERSION 2.0.0 LANGUAGES C)

# Suffix appended to the version string reported at runtime and baked into
# version.h -- marks development builds between releases. Cleared by the
# release commit and restored by the next development-cycle bump.
set(KATHERINE_VERSION_SUFFIX "-dev")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(KatherineFeatures)
include(KatherineWarnings)

# Oldest Doxygen able to process docs/Doxyfile (the file's format version).
set(KATHERINE_DOXYGEN_MINIMUM_VERSION 1.8.13)

# Oldest Python and Cython able to build the bindings in python/. The
# Python floor follows the current Cython 3.x line, which requires 3.8.
set(KATHERINE_PYTHON_MINIMUM_VERSION 3.8)
set(KATHERINE_CYTHON_MINIMUM_VERSION 3.0)

katherine_declare_feature(BUILD_CXX        "build C++ binaries"      DEFAULT ON  LEGACY BUILD_CXX)
katherine_declare_feature(BUILD_PYTHON     "build Python 3 bindings" DEFAULT OFF LEGACY BUILD_PYTHON AUTO_DETECTED)
katherine_declare_feature(BUILD_EXAMPLES   "build example programs"  DEFAULT ON  LEGACY BUILD_EXAMPLES)
katherine_declare_feature(BUILD_DOXYGEN    "build API documentation" DEFAULT OFF AUTO_DETECTED)
katherine_declare_feature(BUILD_TESTS      "build unit tests"        DEFAULT ON)
katherine_declare_feature(BUILD_BENCHMARKS "build benchmark suite"   DEFAULT ON)
katherine_declare_feature(BUILD_EMULATOR   "build protocol emulator" DEFAULT ON)
katherine_feature_summary()

# The dialect this project's own binaries are built with. The guarded
# defaults are the language floors themselves, so any newer standard can be
# forced at configure time (-DCMAKE_C_STANDARD=23 and friends). A forced
# standard below the floor is rejected here explicitly: the floors declared
# on the library targets with compile features would otherwise re-raise it
# silently, building something other than what was asked for. The standards
# do not compare numerically (90 and 99 predate 11), hence the value tests.
if(NOT DEFINED CMAKE_C_STANDARD)
    set(CMAKE_C_STANDARD 11)
elseif(CMAKE_C_STANDARD EQUAL 90 OR CMAKE_C_STANDARD EQUAL 99)
    message(FATAL_ERROR "libkatherine requires C11 or newer; CMAKE_C_STANDARD=${CMAKE_C_STANDARD} was requested.")
endif()
if(NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 11)
elseif(CMAKE_CXX_STANDARD EQUAL 98)
    message(FATAL_ERROR "libkatherine requires C++11 or newer; CMAKE_CXX_STANDARD=${CMAKE_CXX_STANDARD} was requested.")
endif()
set(CMAKE_C_STANDARD_REQUIRED   ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Flatten build outputs into the build root for convenient in-tree use --
# but only in standalone builds: when consumed with add_subdirectory or
# FetchContent, the parent project's layout must be respected.
if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
endif()

# Compilation database, for editors and every clang-based tool that reads
# one. Written by the Makefile and Ninja generators only; the IDE
# generators carry the same information in their own project files, and
# ignore this. Nothing points at it from the source directory on purpose:
# clangd searches the ancestors of the file it is opening and the "build"
# subdirectory of each, so a build directory named build/ is found
# unaided. Anywhere else, name it to the editor -- the clangd flag is
# --compile-commands-dir, or a CompilationDatabase entry in .clangd.
#
# An explicit ON or OFF from the caller is respected, but an empty value
# is not an answer -- a cache written by an older configure can carry one
# -- so it counts the same as no answer at all.
if(PROJECT_IS_TOP_LEVEL AND "${CMAKE_EXPORT_COMPILE_COMMANDS}" STREQUAL "")
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

include(GNUInstallDirs)

# Directory spellings for the pkg-config templates in c/ and cxx/: keep the
# ${prefix}-relative form (relocatable via pkg-config --define-prefix)
# unless the install directory was overridden with an absolute path.
foreach(dir LIBDIR INCLUDEDIR)
    if(IS_ABSOLUTE "${CMAKE_INSTALL_${dir}}")
        set(KATHERINE_PC_${dir} "${CMAKE_INSTALL_${dir}}")
    else()
        set(KATHERINE_PC_${dir} "\${prefix}/${CMAKE_INSTALL_${dir}}")
    endif()
endforeach()

# Testing must be enabled at the source root (before the subdirectories
# register their tests), otherwise the build root gets no CTest manifest.
# Benchmarks register through add_test() exactly like tests do, so either
# feature turns this on.
if(KATHERINE_BUILD_TESTS OR KATHERINE_BUILD_BENCHMARKS)
    enable_testing()
endif()

add_subdirectory(c)

if(KATHERINE_BUILD_CXX)
    add_subdirectory(cxx)
endif()

if(KATHERINE_BUILD_PYTHON)
    add_subdirectory(python)
endif()

add_subdirectory(tools)

# Added last: the documentation build consumes the file lists exported by
# the c/ and cxx/ directories.
if(KATHERINE_BUILD_DOXYGEN)
    add_subdirectory(docs)
endif()

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
