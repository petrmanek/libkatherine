# Cython declarations for katherine/error.h.
# Created 25.8.26 by Petr Mánek.
#
# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

cdef extern from 'katherine/error.h':
    ctypedef enum katherine_error_t:
        KATHERINE_E_OK
        KATHERINE_E_TIMEOUT
        KATHERINE_E_IO
        KATHERINE_E_CLOSED
        KATHERINE_E_ADDR
        KATHERINE_E_BAD_CRD
        KATHERINE_E_STRAY
        KATHERINE_E_PROTO
        KATHERINE_E_UNSUPPORTED
        KATHERINE_E_BAD_CHIP
        KATHERINE_E_STATE
        KATHERINE_E_HW_UNKNOWN
        KATHERINE_E_INVAL
        KATHERINE_E_NOMEM
        KATHERINE_E_SYSTEM

    const char *katherine_strerror(int error)
