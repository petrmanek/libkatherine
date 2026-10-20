# Cython declarations for katherine/udp.h.
# Created 24.8.26 by Petr Mánek.
#
# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

from libc.stdint cimport uint16_t, uint32_t

from ctransport cimport katherine_discovered_t

cdef extern from 'katherine/udp.h':
    cdef int KATHERINE_DISCOVER_MIN_PORT
    int katherine_device_discover_udp(const char *address, uint16_t reply_port, uint32_t timeout_ms, katherine_discovered_t *out, size_t *count)
