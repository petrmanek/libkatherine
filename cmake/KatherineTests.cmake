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
                    [NEEDS <FEATURE>]
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

  NEEDS <FEATURE> builds and registers the test only where the option
  KATHERINE_<FEATURE> is on; elsewhere the test is absent, not failing.
  A test that drives a transport says NEEDS TRANSPORT_UDP, which is what
  lets a build of another transport still run the tests that need none.
  The target does not exist in that case, so anything the caller does to
  it afterwards belongs inside an if(TARGET).

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
    cmake_parse_arguments(PARSE_ARGV 0 ARG "STANDALONE" "NAME;NEEDS" "SOURCES;ARGS;LABELS;LIBRARIES;PROPERTIES")
    if(NOT ARG_NAME)
        message(FATAL_ERROR "katherine_add_test: NAME is required")
    endif()
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "katherine_add_test(${ARG_NAME}): SOURCES is required")
    endif()
    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "katherine_add_test(${ARG_NAME}): unknown arguments: ${ARG_UNPARSED_ARGUMENTS}")
    endif()

    # Recorded for katherine_check_tests_registered() below, and recorded
    # before the NEEDS check so that a test skipped for want of a feature
    # still counts as registered rather than as forgotten.
    foreach(source IN LISTS ARG_SOURCES)
        get_filename_component(source "${source}" ABSOLUTE)
        set_property(GLOBAL APPEND PROPERTY KATHERINE_REGISTERED_TEST_SOURCES "${source}")
    endforeach()

    # NEEDS <FEATURE> builds this test only where KATHERINE_<FEATURE> is on.
    # A test that drives a transport says so here rather than being wrapped
    # in an if(): the requirement belongs beside the test, and a wrapped
    # block quietly collects tests that no longer need it.
    if(ARG_NEEDS AND NOT KATHERINE_${ARG_NEEDS})
        return()
    endif()


    add_executable(${ARG_NAME} ${ARG_SOURCES})
    # STANDALONE builds the library sources it names into itself instead of
    # linking them: a test that substitutes a dependency has to, and linking
    # the library as well would define every one of those symbols twice.
    if(ARG_STANDALONE)
        target_link_libraries(${ARG_NAME} PRIVATE katherine_private Threads::Threads)
        # The public headers without the library behind them, and the export
        # macro set as the library sets it: KATHERINE_EXPORTED otherwise
        # declares as dllimport what this target defines itself.
        target_include_directories(${ARG_NAME} PRIVATE
            $<TARGET_PROPERTY:katherine,INTERFACE_INCLUDE_DIRECTORIES>)
        target_compile_definitions(${ARG_NAME} PRIVATE katherine_EXPORTS)
    else()
        # The archive rather than the shared object: a test may call what the
        # library does not publish, and hidden visibility would refuse it.
        target_link_libraries(${ARG_NAME} PRIVATE katherine_static katherine_private Threads::Threads)
    endif()
    # ktest.h's floating-point comparisons call nextafter(), which glibc keeps
    # in a separate libm. Elsewhere -- MSVC among them -- the maths functions
    # live in the C runtime and there is no such library to find.
    if(KATHERINE_LIBM)
        target_link_libraries(${ARG_NAME} PRIVATE ${KATHERINE_LIBM})
    endif()
    # kports_available() calls Winsock from the test's own translation unit,
    # and the library links ws2_32 PRIVATE.
    if(WIN32 OR MINGW)
        target_link_libraries(${ARG_NAME} PRIVATE ws2_32)
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
