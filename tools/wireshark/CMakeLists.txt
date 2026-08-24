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
