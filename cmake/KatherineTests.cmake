# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

#[=======================================================================[
KatherineTests
--------------

Test registration facility for the Katherine project.

katherine_add_test(NAME <name>
                    SOURCES <source>...
                    LABELS <label>...)

  Builds an executable called <name> from <source>... and registers it
  with CTest via add_test(). The executable is linked against the
  katherine library and Threads::Threads (some tests drive a mock UDP
  readout on a background thread), and privately includes c/src so
  tests may reach the library's internal headers alongside its public
  API. LABELS is recorded as the test's CTest LABELS property, so e.g.
  `ctest -L unit` can select a subset of the registered tests.
#]=======================================================================]

include_guard(GLOBAL)

# Needed by every test executable; resolved once here rather than in each
# katherine_add_test() call.
find_package(Threads REQUIRED)

function(katherine_add_test)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "" "NAME" "SOURCES;LABELS")
    if(NOT ARG_NAME)
        message(FATAL_ERROR "katherine_add_test: NAME is required")
    endif()
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "katherine_add_test(${ARG_NAME}): SOURCES is required")
    endif()
    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "katherine_add_test(${ARG_NAME}): unknown arguments: ${ARG_UNPARSED_ARGUMENTS}")
    endif()

    add_executable(${ARG_NAME} ${ARG_SOURCES})
    target_link_libraries(${ARG_NAME} PRIVATE katherine Threads::Threads)
    target_include_directories(${ARG_NAME} PRIVATE "${PROJECT_SOURCE_DIR}/c/src")

    add_test(NAME ${ARG_NAME} COMMAND ${ARG_NAME})
    if(ARG_LABELS)
        set_tests_properties(${ARG_NAME} PROPERTIES LABELS "${ARG_LABELS}")
    endif()
endfunction()
