# Katherine Control Library
#
# This file was created on 24.2.19 by Petr Manek.
# 
# Contents of this file are copyrighted and subject to license
# conditions specified in the LICENSE file located in the top
# directory.

from libc.stdint cimport uint32_t

cdef extern from 'katherine/px_config.h':
    ctypedef struct katherine_px_config_t:
        uint32_t words[16384]

    int katherine_px_config_load_bmc_file(katherine_px_config_t *px_config, const char *file_path)
    int katherine_px_config_load_bpc_file(katherine_px_config_t *px_config, const char *file_path)
