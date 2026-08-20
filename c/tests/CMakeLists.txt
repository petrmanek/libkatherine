# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

# These tests talk POSIX sockets and pthreads directly, so they cannot
# build on Windows yet. Keep the guard scoped tightly around them so that
# future portable tests can be registered unconditionally below it.
if(NOT WIN32)
    katherine_add_test(NAME test_tp SOURCES test_tp.c LABELS unit)
    katherine_add_test(NAME test_issue16 SOURCES test_issue16.c LABELS unit)
endif()
