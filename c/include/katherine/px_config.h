/**
 * \file
 * \brief Functions related to pixel matrix configuration format.
 * \author Petr Mánek
 * \date 9.6.18
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <katherine/global.h>
#include <katherine/error.h>
#include <katherine/px.h>

/**
 * \defgroup katherine_px_config Pixel configuration
 * \ingroup katherine_c_api
 * \brief The per-pixel threshold and mask matrix uploaded to an ASIC.
 */

/**
 * \addtogroup katherine_px_config
 * \{
 */

#ifdef __cplusplus
extern "C" {
#endif

// PACKED(typedef struct katherine_bmc_px {
// unsigned char mask : 1;
// unsigned char loc_thl : 4;
// unsigned char test : 1;
// unsigned char : 2;
// }) katherine_bmc_px_t;

/// One pixel's settings in the BMC layout: the bitfield commented out above.
typedef uint8_t katherine_bmc_px_t;

/** A whole matrix in the BMC layout, one byte per pixel, as a BurdaMan .bmc file holds it. */
typedef struct katherine_bmc {
    katherine_bmc_px_t px_config[65536]; ///< One configuration byte per pixel of the matrix.
} katherine_bmc_t;

/** A whole matrix in the form the readout is sent, which is what katherine_set_all_pixel_config() uploads. */
typedef struct katherine_px_config {
    uint32_t words[16384]; ///< The matrix packed as the readout takes it; the accessors below address one pixel at a time.
} katherine_px_config_t;

/**
 * Render a digest of the pixel configuration matrix: the word count and a
 * 64-bit XOR fold of its contents, never the 16384 words themselves. See
 * katherine_px_config_snprint() (repr.c) for how the fold is computed; it is
 * a fingerprint for telling two matrices apart at a glance in a log, not a
 * checksum, and carries no error-detection guarantee.
 */
KATHERINE_EXPORTED int
katherine_px_config_snprint(char *buf, size_t cap, const katherine_px_config_t *v);

/// One pixel's settings in the BPC layout: the same bits as katherine_bmc_px_t with the four threshold bits in the opposite order.
typedef uint8_t katherine_bpc_px_t;

/** A whole matrix in the BPC layout, one byte per pixel, as a Pixet .bpc file holds it. */
typedef struct katherine_bpc {
    katherine_bpc_px_t px_config[65536]; ///< One configuration byte per pixel of the matrix.
} katherine_bpc_t;


// Loading full matrix configuration en masse (either from a binary file or a memory buffer):

KATHERINE_EXPORTED katherine_error_t
katherine_px_config_load_bmc_file(katherine_px_config_t *px_config, const char *file_path);

KATHERINE_EXPORTED katherine_error_t
katherine_px_config_load_bmc_data(katherine_px_config_t *px_config, const katherine_bmc_t *bmc);

KATHERINE_EXPORTED katherine_error_t
katherine_px_config_load_bpc_file(katherine_px_config_t *px_config, const char *file_path);

KATHERINE_EXPORTED katherine_error_t
katherine_px_config_load_bpc_data(katherine_px_config_t *px_config, const katherine_bpc_t *bpc);


// Manipulation of values in the registers of individual pixels:

KATHERINE_EXPORTED void
katherine_px_config_set_test_bit(katherine_px_config_t *px_config, katherine_tpx3_coord_t coord, bool enabled);

KATHERINE_EXPORTED bool
katherine_px_config_get_test_bit(const katherine_px_config_t *px_config, katherine_tpx3_coord_t coord);

KATHERINE_EXPORTED void
katherine_px_config_set_mask_bit(katherine_px_config_t *px_config, katherine_tpx3_coord_t coord, bool masked);

KATHERINE_EXPORTED bool
katherine_px_config_get_mask_bit(const katherine_px_config_t *px_config, katherine_tpx3_coord_t coord);

KATHERINE_EXPORTED void
katherine_px_config_set_loc_thl(katherine_px_config_t *px_config, katherine_tpx3_coord_t coord, uint8_t loc_thl);

KATHERINE_EXPORTED uint8_t
katherine_px_config_get_loc_thl(const katherine_px_config_t *px_config, katherine_tpx3_coord_t coord);

#ifdef __cplusplus
}
#endif

/** \} */
