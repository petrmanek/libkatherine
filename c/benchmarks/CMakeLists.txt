# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

# B1: pure decode throughput, one canned in-RAM buffer, no sockets -- builds
# and runs everywhere.
katherine_add_bench(NAME bench_decode SOURCES bench_decode.c)

# B2: UDP loopback throughput and command round trips, through the public
# katherine_udp_* API only (like c/tests/test_udp_pinning.c) -- builds and
# runs everywhere, no platform guard needed. It binds fixed ports, so it
# takes a block from the suite's registry and needs that header on its
# include path; see c/tests/kports.h for why the numbers are not each
# benchmark's to choose.
katherine_add_bench(NAME bench_udp SOURCES bench_udp.c)
target_include_directories(bench_udp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../tests")
