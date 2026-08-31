# Cython declarations for katherine/toa.h.
# Created 30.8.26 by Petr Mánek.
#
# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

from libc.stdint cimport uint8_t, uint64_t
from cconfig cimport katherine_freq_t

cdef extern from 'katherine/toa.h':
    uint8_t katherine_tpx3_toa_coarse_tick_to_fine_ticks(katherine_freq_t freq)
    uint8_t katherine_tpx3_toa_coarse_tick_to_fine_shift(katherine_freq_t freq)
    uint64_t katherine_tpx3_toa_epoch_bias(uint8_t coarse_tick_to_fine_shift)
    void katherine_tpx3_timestamp_to_seconds(uint64_t timestamp, uint64_t *sec, double *nsec)
    void katherine_tpx3_timestamp_to_toa_ftoa(uint8_t coarse_tick_to_fine_shift, uint8_t phase_offset, uint64_t timestamp, uint64_t *toa, uint8_t *ftoa)
