# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

# Pure bitfield/wire-format vectors: no sockets or threads, builds everywhere.
katherine_add_test(NAME test_bitfields SOURCES test_bitfields.c LABELS unit)

# DAC register range validation (katherine_dacs_validate(), config.c) against
# the per-DAC maxima of Tpx3 manual Table 11. Plain, portable C, no sockets or
# threads, builds everywhere -- same shape as test_bitfields above.
katherine_add_test(NAME test_dacs_validate SOURCES test_dacs_validate.c LABELS unit)

# Timepix3 manual Table 17: the phase count the clock divider actually grants,
# and the one frequency at which fine time stamping means anything. Vectors
# come from the manual page rather than from the implementation.
katherine_add_test(NAME test_clock_phases SOURCES test_clock_phases.c LABELS unit)

katherine_add_test(NAME test_timestamp SOURCES test_timestamp.c LABELS unit)

katherine_add_test(NAME test_phase_correction SOURCES test_phase_correction.c LABELS unit)

katherine_add_test(NAME test_device_info SOURCES test_device_info.c LABELS unit)

# Length checking of the pixel-configuration loaders. Writes a scratch file
# next to the test binary and removes it, so it claims no global resource and
# two build trees cannot collide.
katherine_add_test(NAME test_px_config_load SOURCES test_px_config_load.c LABELS unit)

# Golden byte-vector tests for the command encoders. Sockets, but only
# through the public katherine_udp_* API and on uncommon high ports of its
# own (see the file for the exact pair), so it builds everywhere and claims
# no global resource.
katherine_add_test(NAME test_cmd_encoders SOURCES test_cmd_encoders.c LABELS unit)

# Golden-string tests for the debug stringification facility (repr.c):
# katherine_*_snprint() and katherine_str_*() for every covered public
# struct/enum. Plain, portable C plus two katherine_udp_t fixtures on
# uncommon high loopback ports of their own (never the device's fixed
# 1555/1556), so it builds and runs everywhere and claims no global
# resource, exactly like test_cmd_encoders above.
katherine_add_test(NAME test_repr SOURCES test_repr.c LABELS unit)

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

# The acquisition-mode command and the sensor-register flush that carries it to
# the sensor, as byte-exact vectors taken from vendor-tool captures. Same
# threaded mock readout as test_tp, serving the two datagrams one call sends.
katherine_add_test(NAME test_acq_mode SOURCES test_acq_mode.c LABELS unit)

# The inquiries a running acquisition refuses. Needs no readout: the guard sits
# ahead of the session lock, so an uninitialized device exercises it -- which
# also pins that the guard comes first.
katherine_add_test(NAME test_acq_guard SOURCES test_acq_guard.c LABELS unit)

# Correlation of command responses with their requests (the exchange
# primitives of protocol/cmd_interface.h) against a mock readout on its own
# uncommon high ports. One case needs the mock on a background thread, as
# test_tp does and for the same reason -- the flush, the send and the receive
# of a transaction happen inside one library call.
katherine_add_test(NAME test_cmd_transact SOURCES test_cmd_transact.c LABELS unit)
# One further case drives the protocol emulator in memory, to check that the
# emulated readout answers the two commands the firmware answers under a
# substituted identifier the same way it does. It needs the emulator in the
# build, so it is compiled in only when the feature is on -- the rest of the
# file needs nothing but sockets and is registered unconditionally.
if(KATHERINE_BUILD_EMULATOR)
    target_compile_definitions(test_cmd_transact PRIVATE KATHERINE_TEST_EMULATOR)
endif()

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
    # strerror() has no portable replacement that is also safe under
    # concurrent access from other threads of the same process; this test
    # is single-threaded, so the CRT's objection does not apply.
    target_compile_definitions(test_e2e_acq PRIVATE $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)

    # Recovery of a pixel-configuration upload whose acknowledgement the
    # daemon is told to drop. Same daemon, same fixed ports, hence the same
    # properties as above.
    katherine_add_test(NAME test_px_recovery SOURCES test_px_recovery.c
        ARGS "$<TARGET_FILE:ksim>"
        LABELS e2e
        PROPERTIES RUN_SERIAL TRUE TIMEOUT 120 SKIP_RETURN_CODE 77)
    add_dependencies(test_px_recovery ksim)
    # See test_e2e_acq above: same single-threaded strerror() usage.
    target_compile_definitions(test_px_recovery PRIVATE $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)
endif()
