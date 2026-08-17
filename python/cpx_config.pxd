# Katherine Control Library
#
# This file was created on 24.2.19 by Petr Manek.
# 
# Contents of this file are copyrighted and subject to license
# conditions specified in the LICENSE file located in the top
# directory.

from libcpp cimport bool
from libc.stdint cimport uint8_t, uint32_t

cdef extern from 'katherine/px.h':
    ctypedef struct katherine_coord_t:
        uint8_t x
        uint8_t y

cdef extern from 'katherine/px_config.h':
    ctypedef struct katherine_px_config_t:
        uint32_t words[16384]

    int katherine_px_config_load_bmc_file(katherine_px_config_t *px_config, const char *file_path)
    int katherine_px_config_load_bpc_file(katherine_px_config_t *px_config, const char *file_path)
    void katherine_px_config_set_test_bit(katherine_px_config_t *px_config, katherine_coord_t coord, bool enabled)
    bool katherine_px_config_get_test_bit(const katherine_px_config_t *px_config, katherine_coord_t coord)
    void katherine_px_config_set_mask_bit(katherine_px_config_t *px_config, katherine_coord_t coord, bool masked)
    bool katherine_px_config_get_mask_bit(const katherine_px_config_t *px_config, katherine_coord_t coord)
    void katherine_px_config_set_loc_thl(katherine_px_config_t *px_config, katherine_coord_t coord, uint8_t loc_thl)
    uint8_t katherine_px_config_get_loc_thl(const katherine_px_config_t *px_config, katherine_coord_t coord)
