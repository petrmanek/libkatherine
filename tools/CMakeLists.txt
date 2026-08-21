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
