# Cython declarations for katherine/discover.h.
# Created 18.9.26 by Petr Mánek.
#
# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

from libc.stdint cimport uint16_t, uint32_t

cdef extern from 'katherine/discover.h':
    cdef int KATHERINE_DISCOVER_MIN_PORT

    ctypedef struct katherine_discovered_t:
        char *addr
        bint legacy

    int katherine_device_discover_udp(const char *addr, uint16_t reply_port, uint32_t timeout_ms, katherine_discovered_t *out, size_t *count)
