# Cython declarations for katherine/version.h.
# Created 24.8.26 by Petr Mánek.
#
# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

from libc.stdint cimport uint64_t

cdef extern from 'katherine/version.h':
    uint64_t katherine_version()
    const char *katherine_version_string()
