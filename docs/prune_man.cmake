# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT
#
# Removes the manual pages doxygen emits that must not reach a man hierarchy.
#
# MAN_LINKS is what makes `man 3 katherine_device_init` resolve: doxygen writes
# a real page per group, struct and file, and a one-line `.so` redirect for
# every entity documented inside one. There is no way to narrow that -- the
# five MAN_* settings are all doxygen offers -- and "every entity" includes
# struct fields, under their bare member names. Left alone, this project
# installs a name.3, an x.3, a words.3 and a bytes.3.
#
# Two rules clear it. A page must be named for the library, which drops the
# field pages and the pages of source files; and a redirect must point at a
# page that survives the first rule, which drops the internal statics and
# tables documented in the .c files -- those redirect into a file page, and
# are not API even where they carry the prefix.
#
# Usage: cmake -DMAN_DIR=<dir>/man3 -P prune_man.cmake

# Script mode has no project to inherit policies from, so they sit at their
# OLD defaults and IN_LIST below is not recognized as an operator -- on a
# CMake whose defaults differ from the one this was written on, that is a hard
# error rather than a warning. Declaring the same minimum as the top-level
# project keeps the script and the build agreeing about which CMake they need.
cmake_minimum_required(VERSION 3.21)

if(NOT MAN_DIR)
    message(FATAL_ERROR "MAN_DIR is required")
endif()

file(GLOB pages "${MAN_DIR}/*.3")
if(NOT pages)
    message(FATAL_ERROR "no manual pages in ${MAN_DIR}")
endif()

# Named for the library, and not the page of a source file.
set(public "")
foreach(page IN LISTS pages)
    get_filename_component(name "${page}" NAME)
    if(NOT name MATCHES "^(katherine|KATHERINE)" OR name MATCHES "\\.(c|h|hpp)\\.3$")
        continue()
    endif()
    file(STRINGS "${page}" first LIMIT_COUNT 1)
    if(NOT first MATCHES "^\\.so ")
        list(APPEND public "${name}")
    endif()
endforeach()

# Each regex match overwrites CMAKE_MATCH_1, so the redirect is matched on its
# own and the capture read immediately, with no second match in between.
set(keep ${public})
foreach(page IN LISTS pages)
    get_filename_component(name "${page}" NAME)
    if(NOT name MATCHES "^(katherine|KATHERINE)")
        continue()
    endif()
    file(STRINGS "${page}" first LIMIT_COUNT 1)
    if(first MATCHES "^\\.so man3/(.+)$")
        set(target "${CMAKE_MATCH_1}")
        if(target IN_LIST public)
            list(APPEND keep "${name}")
        endif()
    endif()
endforeach()

set(removed 0)
foreach(page IN LISTS pages)
    get_filename_component(name "${page}" NAME)
    if(NOT name IN_LIST keep)
        file(REMOVE "${page}")
        math(EXPR removed "${removed} + 1")
    endif()
endforeach()

list(LENGTH keep kept)
message(STATUS "Manual pages: kept ${kept}, removed ${removed}")
