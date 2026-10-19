# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

# ksim hosts the protocol emulator on real UDP sockets, so it needs the
# emulator built in. Its platform-specific bits (long-option parsing, the
# stop signal, the monotonic clock) are wrapped in small private headers
# under ksim/, so it builds on Windows as well as on POSIX.
if(KATHERINE_BUILD_EMULATOR)
    add_subdirectory(ksim)
endif()

add_subdirectory(debuggers)
add_subdirectory(wireshark)

# The error-code indexer decides what every \retval block in the library is
# checked against. Its self-test needs python-clang, which nothing else here
# needs, so a missing one is a skip rather than a failure.
if(KATHERINE_BUILD_TESTS)
    # Not through python/, which is only configured when the bindings are.
    find_package(Python3 COMPONENTS Interpreter QUIET)

    if(Python3_Interpreter_FOUND)
        add_test(NAME test_errdoc_selftest
                 COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/katherine_errdoc_selftest.py")
        set_tests_properties(test_errdoc_selftest PROPERTIES
            LABELS "unit;tools"
            TIMEOUT 120
            SKIP_RETURN_CODE 77)
    endif()
endif()
