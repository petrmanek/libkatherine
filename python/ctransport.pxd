# Cython declarations for katherine/transport.h.
# Created 18.10.26 by Petr Mánek.
#
# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

from libc.stdint cimport uint16_t, uint32_t, uint64_t

cdef extern from 'katherine/transport.h':
    ctypedef struct katherine_transport_t:
        bint strict_ack
        uint64_t stray_command_responses

    void katherine_transport_set_strict_ack(katherine_transport_t *transport, bint strict)
    int katherine_transport_get_last_os_error(const katherine_transport_t *transport)
    int katherine_transport_set_timeout(katherine_transport_t *transport, uint32_t timeout_ms)
    int katherine_transport_get_dropped_input(const katherine_transport_t *transport, uint64_t *count)
