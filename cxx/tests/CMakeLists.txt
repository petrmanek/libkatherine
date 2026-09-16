# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

# Smoke coverage for the C++ bindings. Linking katherinexx brings the headers
# and their C++ standard floor along as usage requirements, so these targets
# compile at whatever that floor is rather than at a number repeated here.
#
# The whole C++ surface except the two types that need a socket on a fixed
# port. Claims no global resource, so it stays a unit test and runs in the
# standards legs of CI, which select on that label.
katherine_add_test(NAME test_cxx_smoke
    SOURCES test_cxx_smoke.cpp
    LABELS unit
    LIBRARIES katherinexx
)

# katherine::device and katherine::acquisition. katherine_device_init() binds
# the fixed ports 1555/1556, which is a global resource and the same one the
# emulator tests claim, so this must not run beside them (RUN_SERIAL). It
# needs no daemon: every call fails on the receive timeout, which is the
# point, and the timeout below is a generous multiple of the second or so a
# healthy run takes.
katherine_add_test(NAME test_cxx_device
    SOURCES test_cxx_device.cpp
    LABELS e2e
    LIBRARIES katherinexx
    PROPERTIES RUN_SERIAL TRUE TIMEOUT 120
)

# ktest.h lives with the C tests and is the project's whole test harness; it
# is valid C++ as written, so the C++ suite reports in the same TAP dialect
# rather than growing a second one.
foreach(target test_cxx_smoke test_cxx_device)
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/c/tests")
endforeach()

# Both C++ test sources are registered unconditionally.
katherine_check_tests_registered()
