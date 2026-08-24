# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

# Pure bitfield/wire-format vectors: no sockets or threads, builds everywhere.
katherine_add_test(NAME test_bitfields SOURCES test_bitfields.c LABELS unit)

# Golden byte-vector tests for the command encoders. Sockets, but only
# through the public katherine_udp_* API and on uncommon high ports of its
# own (see the file for the exact pair), so it builds everywhere and claims
# no global resource.
katherine_add_test(NAME test_cmd_encoders SOURCES test_cmd_encoders.c LABELS unit)

# Regression tests driving the real read loop over crafted measurement-data
# streams queued in through the public katherine_udp_* API, each on its own
# fixed pair of uncommon high ports (see the individual files). No hardware,
# no daemon, no global resource.
katherine_add_test(NAME test_issue16 SOURCES test_issue16.c LABELS unit)
katherine_add_test(NAME test_md_decode SOURCES test_md_decode.c LABELS unit)

# Test-pulse validation. Its byte-exact datagram check drives a mock readout
# through the public katherine_udp_* API, on its own thread (kthread.h) since
# katherine_set_test_pulses() sends and waits for the acknowledgement inside
# one opaque, blocking library call -- see test_tp.c for why that rules out
# the inline, lockstep ping-pong bench_udp.c uses instead.
katherine_add_test(NAME test_tp SOURCES test_tp.c LABELS unit)

# Remote-address pinning of the UDP layer. Sockets, but only through the
# public katherine_udp_* API and on uncommon high ports of its own, so it
# builds everywhere and claims nothing another test could want: no daemon, no
# threads, and none of the fixed ports that would make it RUN_SERIAL.
katherine_add_test(NAME test_udp_pinning SOURCES test_udp_pinning.c LABELS unit)

# End-to-end acquisition against the ksim daemon, which hosts the protocol
# emulator on real UDP sockets -- hence the guard on the emulator feature,
# which is what brings the ksim target into the build. The test drives only
# the public API plus the two private header helpers (kspawn.h, msleep.h) and
# is portable by design, so it is registered unconditionally: the daemon
# binds a secondary loopback address the host may not offer, and the test
# answers that with a run-time skip (exit code 77) rather than a build-time
# guard.
#
# The fixed ports 1555/1556 are a global resource, so the test must not run
# beside anything else that binds them (RUN_SERIAL); the timeout is a
# generous multiple of the couple of seconds a healthy run takes.
if(KATHERINE_BUILD_EMULATOR)
    katherine_add_test(NAME test_e2e_acq SOURCES test_e2e_acq.c
        ARGS "$<TARGET_FILE:ksim>"
        LABELS e2e
        PROPERTIES RUN_SERIAL TRUE TIMEOUT 120 SKIP_RETURN_CODE 77)
    # tools/ is added after c/, so the ksim target does not exist yet: both
    # the generator expression above and this dependency are resolved at
    # generate time, by when it does. Without the dependency, building the
    # test target alone would leave the daemon it runs unbuilt.
    add_dependencies(test_e2e_acq ksim)

    # Recovery of a pixel-configuration upload whose acknowledgement the
    # daemon is told to drop. Same daemon, same fixed ports, hence the same
    # properties as above.
    katherine_add_test(NAME test_px_recovery SOURCES test_px_recovery.c
        ARGS "$<TARGET_FILE:ksim>"
        LABELS e2e
        PROPERTIES RUN_SERIAL TRUE TIMEOUT 120 SKIP_RETURN_CODE 77)
    add_dependencies(test_px_recovery ksim)
endif()
