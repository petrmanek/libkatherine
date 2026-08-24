# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

# B1: pure decode throughput, one canned in-RAM buffer, no sockets -- builds
# and runs everywhere.
katherine_add_bench(NAME bench_decode SOURCES bench_decode.c)

