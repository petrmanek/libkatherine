# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

# Wireshark loads Lua plugins from its global plugin directory, which on
# every common distribution sits under the library dir (Wireshark's
# About -> Folders lists it as "Global Lua Plugins"). The exact spot is a
# packaging convention rather than an API, so it stays overridable.
# Deliberately STRING, not PATH: cmake absolutizes a relative -D value of a
# PATH-typed cache entry against the shell's working directory, which would
# break anchoring a relative override at the install prefix.
set(KATHERINE_WIRESHARK_PLUGIN_DIR "${CMAKE_INSTALL_LIBDIR}/wireshark/plugins" CACHE STRING
    "Installation directory of the Wireshark dissector (a relative path is anchored at the install prefix)")

install(FILES katherine.lua DESTINATION "${KATHERINE_WIRESHARK_PLUGIN_DIR}")

# The dissector's own check, which needs tshark rather than the library and so
# is registered only where tshark exists. Labelled `tool` rather than `unit`:
# it tests neither the library nor anything the library links against, and a
# host without Wireshark is not a host with a broken build.
find_program(KATHERINE_TSHARK tshark)
if(KATHERINE_BUILD_TESTS AND KATHERINE_TSHARK)
    # Its own find_package: the Python bindings may be off, and this check
    # needs nothing of them but an interpreter.
    find_package(Python3 COMPONENTS Interpreter)
endif()
if(KATHERINE_BUILD_TESTS AND KATHERINE_TSHARK AND Python3_Interpreter_FOUND)
    add_test(NAME test_wireshark_dissector
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/selftest.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/katherine.lua")
    set_tests_properties(test_wireshark_dissector PROPERTIES LABELS tool)
endif()
