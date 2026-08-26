# Cython declarations for katherine/udp.h.
# Created 24.8.26 by Petr Mánek.
#
# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

from libc.stdint cimport uint16_t, uint32_t, uint64_t

cdef extern from 'katherine/udp.h':
    # Declared field by field rather than opaquely: the correlation counter
    # is public state a caller reads, and Cython accepts a partial struct
    # declaration as long as the storage is sized by sizeof() as here.
    ctypedef struct katherine_udp_t:
        uint64_t stray_command_responses

    int katherine_udp_snprint(char *buf, size_t cap, const katherine_udp_t *v)

    int katherine_udp_init_bound(katherine_udp_t *u, const char *local_addr, uint16_t local_port, const char *remote_addr, uint16_t remote_port, uint32_t timeout_ms)
    void katherine_udp_fini(katherine_udp_t *u)
    int katherine_udp_send_exact(katherine_udp_t *u, const void *data, size_t count)
    int katherine_udp_recv_exact(katherine_udp_t *u, void *data, size_t count)
    int katherine_udp_recv(katherine_udp_t *u, void *data, size_t *count)
    int katherine_udp_recv_nowait(katherine_udp_t *u, void *data, size_t *count)
    int katherine_udp_set_remote(katherine_udp_t *u, const char *remote_addr, uint16_t remote_port)
    void katherine_udp_pin_remote(katherine_udp_t *u)
    void katherine_udp_set_strict_ack(katherine_udp_t *u, bint strict)
