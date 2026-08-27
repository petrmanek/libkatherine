# Cython declarations for katherine/px.h.
# Created 20.8.26 by Petr Mánek.
#
# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

from libc.stdint cimport uint8_t, uint16_t, uint64_t

cdef extern from 'katherine/px.h':
    ctypedef struct katherine_coord_t:
        uint8_t x
        uint8_t y

    int katherine_coord_snprint(char *buf, size_t cap, const katherine_coord_t *v)

    ctypedef struct katherine_px_f_toa_tot_t:
        katherine_coord_t coord
        uint8_t ftoa
        uint64_t toa
        uint16_t tot

    int katherine_px_f_toa_tot_snprint(char *buf, size_t cap, const katherine_px_f_toa_tot_t *v)

    ctypedef struct katherine_px_toa_tot_t:
        katherine_coord_t coord
        uint64_t toa
        uint8_t hit_count
        uint16_t tot

    int katherine_px_toa_tot_snprint(char *buf, size_t cap, const katherine_px_toa_tot_t *v)

    ctypedef struct katherine_px_f_toa_only_t:
        katherine_coord_t coord
        uint8_t ftoa
        uint64_t toa

    int katherine_px_f_toa_only_snprint(char *buf, size_t cap, const katherine_px_f_toa_only_t *v)

    ctypedef struct katherine_px_toa_only_t:
        katherine_coord_t coord
        uint64_t toa
        uint8_t hit_count

    int katherine_px_toa_only_snprint(char *buf, size_t cap, const katherine_px_toa_only_t *v)

    ctypedef struct katherine_px_f_event_itot_t:
        katherine_coord_t coord
        uint16_t event_count
        uint16_t integral_tot

    int katherine_px_f_event_itot_snprint(char *buf, size_t cap, const katherine_px_f_event_itot_t *v)

    ctypedef struct katherine_px_event_itot_t:
        katherine_coord_t coord
        uint8_t hit_count
        uint16_t event_count
        uint16_t integral_tot

    int katherine_px_event_itot_snprint(char *buf, size_t cap, const katherine_px_event_itot_t *v)
