# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

#[=======================================================================[
KatherineBenchmarks
-------------------

Benchmark registration facility for the Katherine project.

katherine_add_bench(NAME <name>
                     SOURCES <source>...
                     [LABELS <label>...]
                     [PROPERTIES <property> <value>...])

  Builds an executable called <name> from <source>... and registers it
  with CTest via add_test(), exactly like katherine_add_test(): linked
  against the katherine library, privately including c/src so a
  benchmark may reach the library's internal headers alongside its
  public API, and carrying the project's pedantic warnings (see
  KatherineWarnings). Every benchmark carries the "bench" CTest label
  in addition to whatever LABELS supplies, so `ctest -L bench` selects
  the whole suite.

  A benchmark reports numbers on stdout; it does not itself decide
  pass or fail on their account, so add_test() is expected to see a
  zero exit code as long as the benchmark's environment lets it run at
  all. Catching a performance regression is tools/bench_compare.py's
  job, run separately over the recorded output -- not CTest's, which
  is why no tolerance or baseline lives here.
#]=======================================================================]

include_guard(GLOBAL)

function(katherine_add_bench)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "" "NAME" "SOURCES;LABELS;PROPERTIES")
    if(NOT ARG_NAME)
        message(FATAL_ERROR "katherine_add_bench: NAME is required")
    endif()
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "katherine_add_bench(${ARG_NAME}): SOURCES is required")
    endif()
    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "katherine_add_bench(${ARG_NAME}): unknown arguments: ${ARG_UNPARSED_ARGUMENTS}")
    endif()

    add_executable(${ARG_NAME} ${ARG_SOURCES})
    target_link_libraries(${ARG_NAME} PRIVATE katherine katherine_private)
    katherine_target_warnings(${ARG_NAME})

    add_test(NAME ${ARG_NAME} COMMAND ${ARG_NAME})
    # The CTest entry is a smoke test -- does the benchmark run and emit its
    # metrics -- not a measurement, so it runs with a token duration. Real
    # measurements invoke the binary directly, whose built-in default was
    # calibrated for stable rates.
    set_property(TEST ${ARG_NAME} PROPERTY ENVIRONMENT "KATHERINE_BENCH_MIN_SECONDS=0.05")
    set(labels "bench")
    if(ARG_LABELS)
        list(APPEND labels ${ARG_LABELS})
    endif()
    set_tests_properties(${ARG_NAME} PROPERTIES LABELS "${labels}")
    if(ARG_PROPERTIES)
        set_tests_properties(${ARG_NAME} PROPERTIES ${ARG_PROPERTIES})
    endif()
endfunction()
