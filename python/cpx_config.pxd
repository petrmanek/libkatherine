# Cython declarations for katherine/px_config.h.
# Created 24.2.19 by Petr Mánek.
#
# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

from libcpp cimport bool
from libc.stdint cimport uint8_t, uint32_t
from cpx cimport katherine_coord_t

cdef extern from 'katherine/px_config.h':
    ctypedef unsigned char katherine_bmc_px_t

    ctypedef struct katherine_bmc_t:
        katherine_bmc_px_t px_config[65536]

    ctypedef struct katherine_px_config_t:
        uint32_t words[16384]

    ctypedef unsigned char katherine_bpc_px_t

    ctypedef struct katherine_bpc_t:
        katherine_bpc_px_t px_config[65536]

    int katherine_px_config_load_bmc_file(katherine_px_config_t *px_config, const char *file_path)
    int katherine_px_config_load_bmc_data(katherine_px_config_t *px_config, const katherine_bmc_t *bmc)
    int katherine_px_config_load_bpc_file(katherine_px_config_t *px_config, const char *file_path)
    int katherine_px_config_load_bpc_data(katherine_px_config_t *px_config, const katherine_bpc_t *bpc)
    void katherine_px_config_set_test_bit(katherine_px_config_t *px_config, katherine_coord_t coord, bool enabled)
    bool katherine_px_config_get_test_bit(const katherine_px_config_t *px_config, katherine_coord_t coord)
    void katherine_px_config_set_mask_bit(katherine_px_config_t *px_config, katherine_coord_t coord, bool masked)
    bool katherine_px_config_get_mask_bit(const katherine_px_config_t *px_config, katherine_coord_t coord)
    void katherine_px_config_set_loc_thl(katherine_px_config_t *px_config, katherine_coord_t coord, uint8_t loc_thl)
    uint8_t katherine_px_config_get_loc_thl(const katherine_px_config_t *px_config, katherine_coord_t coord)
