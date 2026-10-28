# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

# ksim hosts the protocol emulator on real UDP sockets, so it needs both the
# emulator and that transport -- which is what KATHERINE_BUILD_KSIM is. Its
# platform-specific bits (long-option parsing, the stop signal, the monotonic
# clock) are wrapped in small private headers under ksim/, so it builds on
# Windows as well as on POSIX.
if(KATHERINE_BUILD_KSIM)
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

        # The pretty-printers' list of covered types against the renderers
        # c/src defines. Sources only, so it needs neither a debugger nor
        # the library it would attach to.
        add_test(NAME test_debugger_tags
                 COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/debuggers/tags_selftest.py")
        set_tests_properties(test_debugger_tags PROPERTIES
            LABELS "unit;tools"
            TIMEOUT 60)

        # Every katherine_*() a doc block points the reader at. A rename
        # moves the function and leaves the sentence behind, and neither
        # the compiler nor Doxygen reads prose.
        add_test(NAME test_prose_refs
                 COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/check_prose_refs.py")
        set_tests_properties(test_prose_refs PROPERTIES
            LABELS "unit;tools"
            TIMEOUT 60)

        # The house style the compiler cannot check: half-converted error
        # paths, native integer types, and doc blocks that ask rather than
        # name. Scoped to c/src, which is the part that is at zero.
        add_test(NAME test_style
                 COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/check_style.py")
        set_tests_properties(test_style PROPERTIES
            LABELS "unit;tools"
            TIMEOUT 60)

        # A 1.x name the shim keeps and the migration guide does not
        # explain. check_prose_refs.py sweeps prose for names the code does
        # not have; this sweeps the code for names the prose does not.
        add_test(NAME test_migration_parity
                 COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/check_migration_parity.py")
        set_tests_properties(test_migration_parity PROPERTIES
            LABELS "unit;tools"
            TIMEOUT 60)

        # A public symbol that one ASIC owns and that carries no prefix
        # saying so. 2.0 is the last release that may rename one, and the
        # judgement behind WP19 is a reading no pattern reproduces, so the
        # check is against a recorded baseline rather than against a rule.
        add_test(NAME test_asic_prefix
                 COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/check_asic_prefix.py")
        set_tests_properties(test_asic_prefix PROPERTIES
            LABELS "unit;tools"
            TIMEOUT 60)

        # A test case registered under a different #ifdef from the one it
        # compiles under. The case then silently does not run wherever the
        # guard drops it, and the suite stays green.
        add_test(NAME test_guards
                 COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/check_test_guards.py")
        set_tests_properties(test_guards PROPERTIES
            LABELS "unit;tools"
            TIMEOUT 60)
    endif()
endif()
