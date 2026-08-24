# Cython declarations for katherine/udp.h.
# Created 24.8.26 by Petr Mánek.
#
# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

from libc.stdint cimport uint16_t, uint32_t

cdef extern from 'katherine/udp.h':
    ctypedef struct katherine_udp_t:
        pass

    int katherine_udp_init_bound(katherine_udp_t *u, const char *local_addr, uint16_t local_port, const char *remote_addr, uint16_t remote_port, uint32_t timeout_ms)
    void katherine_udp_fini(katherine_udp_t *u)
    int katherine_udp_send_exact(katherine_udp_t *u, const void *data, size_t count)
    int katherine_udp_recv_exact(katherine_udp_t *u, void *data, size_t count)
    int katherine_udp_recv(katherine_udp_t *u, void *data, size_t *count)
    int katherine_udp_set_remote(katherine_udp_t *u, const char *remote_addr, uint16_t remote_port)
    void katherine_udp_pin_remote(katherine_udp_t *u)
