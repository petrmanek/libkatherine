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
                    [ARGS <arg>...]
                    [LABELS <label>...]
                    [PROPERTIES <property> <value>...])

  Builds an executable called <name> from <source>... and registers it
  with CTest via add_test(). The executable is linked against the
  katherine library and Threads::Threads (some tests drive a mock UDP
  readout on a background thread), privately includes c/src so tests
  may reach the library's internal headers alongside its public API,
  and carries the project's pedantic warnings (see KatherineWarnings).
  LABELS is recorded as the test's CTest LABELS property, so e.g.
  `ctest -L unit` can select a subset of the registered tests.

  ARGS is appended to the add_test() command line, which is how a test
  that drives an auxiliary executable is told where to find it (e.g.
  ARGS "$<TARGET_FILE:ksim>"). PROPERTIES is passed on to
  set_tests_properties() verbatim, for the remaining CTest properties a
  test may need -- RUN_SERIAL for one that claims a global resource such
  as a fixed port, TIMEOUT, SKIP_RETURN_CODE for one that can decide at
  run time that its environment cannot host it.

  katherine_check_tests_registered([CONDITIONAL <source>...])

    Fails configuration if a test source in the current directory was never
    passed to katherine_add_test(). Losing a registration is silent -- the
    source still compiles, nothing refers to it, and the suite just shrinks --
    which is how test_compat1.c went three weeks unbuilt. CONDITIONAL names
    the sources registered only under a feature guard, so a deliberate
    omission is written down and an accidental one is not.
#]=======================================================================]

include_guard(GLOBAL)

# Needed by every test executable; resolved once here rather than in each
# katherine_add_test() call.
find_package(Threads REQUIRED)

find_library(KATHERINE_LIBM m)
mark_as_advanced(KATHERINE_LIBM)

function(katherine_add_test)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "" "NAME" "SOURCES;ARGS;LABELS;LIBRARIES;PROPERTIES")
    if(NOT ARG_NAME)
        message(FATAL_ERROR "katherine_add_test: NAME is required")
    endif()
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "katherine_add_test(${ARG_NAME}): SOURCES is required")
    endif()
    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "katherine_add_test(${ARG_NAME}): unknown arguments: ${ARG_UNPARSED_ARGUMENTS}")
    endif()

    # Recorded for katherine_check_tests_registered() below.
    foreach(source IN LISTS ARG_SOURCES)
        get_filename_component(source "${source}" ABSOLUTE)
        set_property(GLOBAL APPEND PROPERTY KATHERINE_REGISTERED_TEST_SOURCES "${source}")
    endforeach()

    add_executable(${ARG_NAME} ${ARG_SOURCES})
    target_link_libraries(${ARG_NAME} PRIVATE katherine katherine_private Threads::Threads)
    # ktest.h's floating-point comparisons call nextafter(), which glibc keeps
    # in a separate libm. Elsewhere -- MSVC among them -- the maths functions
    # live in the C runtime and there is no such library to find.
    if(KATHERINE_LIBM)
        target_link_libraries(${ARG_NAME} PRIVATE ${KATHERINE_LIBM})
    endif()
    if(ARG_LIBRARIES)
        target_link_libraries(${ARG_NAME} PRIVATE ${ARG_LIBRARIES})
    endif()
    katherine_target_warnings(${ARG_NAME})

    add_test(NAME ${ARG_NAME} COMMAND ${ARG_NAME} ${ARG_ARGS})
    if(ARG_LABELS)
        set_tests_properties(${ARG_NAME} PROPERTIES LABELS "${ARG_LABELS}")
    endif()
    if(ARG_PROPERTIES)
        set_tests_properties(${ARG_NAME} PROPERTIES ${ARG_PROPERTIES})
    endif()
endfunction()

function(katherine_check_tests_registered)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "" "" "CONDITIONAL")
    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "katherine_check_tests_registered: unknown arguments: ${ARG_UNPARSED_ARGUMENTS}")
    endif()

    # CONFIGURE_DEPENDS so that adding a source re-runs this, rather than
    # leaving the check answering about the directory as it was.
    file(GLOB sources CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/test_*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp")

    get_property(registered GLOBAL PROPERTY KATHERINE_REGISTERED_TEST_SOURCES)
    foreach(allowed IN LISTS ARG_CONDITIONAL)
        get_filename_component(allowed "${allowed}" ABSOLUTE)
        list(APPEND registered "${allowed}")
    endforeach()

    set(missing "")
    foreach(source IN LISTS sources)
        if(NOT source IN_LIST registered)
            file(RELATIVE_PATH source "${PROJECT_SOURCE_DIR}" "${source}")
            list(APPEND missing "${source}")
        endif()
    endforeach()

    if(missing)
        list(JOIN missing "\n  " missing)
        message(FATAL_ERROR
            "test sources never passed to katherine_add_test():\n  ${missing}\n"
            "Register each, or name it in the CONDITIONAL list of "
            "katherine_check_tests_registered() if its registration is guarded.")
    endif()
endfunction()
