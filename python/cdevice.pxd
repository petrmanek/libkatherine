# Cython declarations for katherine/device.h.
# Created 3.2.19 by Petr Mánek.
#
# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

from libc.stdint cimport uint16_t, uint32_t
from libcpp cimport bool

cimport ctransport

cdef extern from 'katherine/device.h':
    ctypedef struct katherine_discovered_t:
        char *address
        bint legacy
    cdef int KATHERINE_DISCOVER_MIN_PORT
    int katherine_device_discover_udp(const char *address, uint16_t reply_port, uint32_t timeout_ms, katherine_discovered_t *out, size_t *count)
    int katherine_device_discover_usb(katherine_discovered_t *out, size_t *count)

    ctypedef struct katherine_device_t:
        ctransport.katherine_transport_t control_transport

    int katherine_device_snprint(char *buf, size_t cap, const katherine_device_t *v)

    int katherine_device_init(katherine_device_t *device, const char *address, uint32_t flags)
    void katherine_device_fini(katherine_device_t *device)
    int katherine_device_enumerate(katherine_device_t *device)
    bool katherine_device_can_correct_timestamp_phase(const katherine_device_t *device)
