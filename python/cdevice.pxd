# Cython declarations for katherine/device.h.
# Created 3.2.19 by Petr Mánek.
#
# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

from libcpp cimport bool

cdef extern from 'katherine/device.h':
    ctypedef struct katherine_device_t:
        pass

    int katherine_device_snprint(char *buf, size_t cap, const katherine_device_t *v)

    int katherine_device_init(katherine_device_t *device, const char *addr)
    void katherine_device_fini(katherine_device_t *device)
    bool katherine_device_can_correct_timestamp_phase(const katherine_device_t *device)
