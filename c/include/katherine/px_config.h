/* Katherine Control Library
 *
 * This file was created on 9.6.18 by Petr Manek.
 * 
 * Contents of this file are copyrighted and subject to license
 * conditions specified in the LICENSE file located in the top
 * directory.
 */

#pragma once

/**
 * @file
 * @brief Functions related to pixel matrix configuration format.
 */

#include <stdbool.h>
#include <stdint.h>
#include <katherine/global.h>
#include <katherine/px.h>

#ifdef __cplusplus
extern "C" {
#endif

/* PACKED(typedef struct katherine_bmc_px {
    unsigned char mask : 1;
    unsigned char loc_thl : 4;
    unsigned char test : 1;
    unsigned char : 2;
}) katherine_bmc_px_t; */

typedef unsigned char katherine_bmc_px_t;

typedef struct katherine_bmc {
    katherine_bmc_px_t px_config[65536];
} katherine_bmc_t;

typedef struct katherine_px_config {
    uint32_t words[16384];
} katherine_px_config_t;

typedef unsigned char katherine_bpc_px_t;

typedef struct katherine_bpc {
    katherine_bpc_px_t px_config[65536];
} katherine_bpc_t;


// Loading full matrix configuration en masse (either from a binary file or a memory buffer):

KATHERINE_EXPORTED int
katherine_px_config_load_bmc_file(katherine_px_config_t *px_config, const char *file_path);

KATHERINE_EXPORTED int
katherine_px_config_load_bmc_data(katherine_px_config_t *px_config, const katherine_bmc_t *bmc);

KATHERINE_EXPORTED int
katherine_px_config_load_bpc_file(katherine_px_config_t *px_config, const char *file_path);

KATHERINE_EXPORTED int
katherine_px_config_load_bpc_data(katherine_px_config_t *px_config, const katherine_bpc_t *bpc);


// Manipulation of values in the registers of individual pixels:

KATHERINE_EXPORTED void
katherine_px_config_set_test_bit(katherine_px_config_t *px_config, katherine_coord_t coord, bool enabled);

KATHERINE_EXPORTED bool
katherine_px_config_get_test_bit(const katherine_px_config_t *px_config, katherine_coord_t coord);

KATHERINE_EXPORTED void
katherine_px_config_set_mask_bit(katherine_px_config_t *px_config, katherine_coord_t coord, bool masked);

KATHERINE_EXPORTED bool
katherine_px_config_get_mask_bit(const katherine_px_config_t *px_config, katherine_coord_t coord);

KATHERINE_EXPORTED void
katherine_px_config_set_loc_thl(katherine_px_config_t *px_config, katherine_coord_t coord, uint8_t loc_thl);

KATHERINE_EXPORTED uint8_t
katherine_px_config_get_loc_thl(const katherine_px_config_t *px_config, katherine_coord_t coord);

#ifdef __cplusplus
}
#endif
