/**
 * @file
 * @brief Implementation of pixel matrix configuration format.
 * @author Petr Mánek
 * @date 9.6.18
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <katherine/error.h>
#include <katherine/px_config.h>
#include "acq/bitfields.h"

/**
 * Maps the small subset of `<errno.h>` values fopen() can plausibly produce
 * that this library's own domain also names explicitly; anything else is a
 * generic I/O failure, exactly like a short read below.
 * @param err Raw `<errno.h>` value from fopen()
 * @return The mapped enumerator
 */
static katherine_error_t
map_fopen_errno(int err)
{
    switch (err) {
    case EINVAL: return KATHERINE_E_INVAL;
    case ENOMEM: return KATHERINE_E_NOMEM;
    default:     return KATHERINE_E_IO;
    }
}

/* Fields of the per-pixel configuration byte. The packed matrix stores one
   such byte per pixel in the BMC bit layout (see the katherine_bmc_px
   struct at the head of px_config.h; the loaders below only permute the
   local threshold bits of the BPC format). */
#define _BITS_bmc_px_mask_start    0
#define _BITS_bmc_px_mask_mask     MASK(1)
#define _BITS_bmc_px_mask_type     bool

#define _BITS_bmc_px_loc_thl_start 1
#define _BITS_bmc_px_loc_thl_mask  MASK(4)
#define _BITS_bmc_px_loc_thl_type  uint8_t

#define _BITS_bmc_px_test_start    5
#define _BITS_bmc_px_test_mask     MASK(1)
#define _BITS_bmc_px_test_type     bool

/**
 * Load pixel configuration from a BMC file (in BurdaMan format).
 * @param px_config Target configuration matrix.
 * @param file_path BMC file path.
 * @return Error code.
 */
int
katherine_px_config_load_bmc_file(katherine_px_config_t *px_config, const char *file_path)
{
    int res = 0;

    const size_t expected_size = sizeof(katherine_bmc_t);
    FILE *file                 = fopen(file_path, "rb");
    if (file == NULL) {
        res = -(int) map_fopen_errno(errno);
        goto err_fopen;
    }

    katherine_bmc_t *buffer = (katherine_bmc_t *) malloc(expected_size);
    if (buffer == NULL) {
        res = -KATHERINE_E_NOMEM;
        goto err_buffer;
    }

    const size_t actual_size = fread(buffer, 1, expected_size, file);
    if (expected_size != actual_size) {
        res = -KATHERINE_E_IO;
        goto err_fread;
    }

    res = katherine_px_config_load_bmc_data(px_config, buffer);

err_fread:
    free(buffer);
err_buffer:
    fclose(file);
err_fopen:
    return res;
}

/**
 * Load pixel configuration from a BMC file contents (in BurdaMan format).
 * @param px_config Target configuration matrix.
 * @param bmc BMC file data.
 * @return Error code.
 */
int
katherine_px_config_load_bmc_data(katherine_px_config_t *px_config, const katherine_bmc_t *bmc)
{
    // Reset the pixel values.
    memset(&px_config->words, 0, 65536);

    int x, y;
    uint32_t *dest           = (uint32_t *) px_config->words;
    const unsigned char *src = (const unsigned char *) bmc->px_config;

    // Parse data from BMC format.
    for (int i = 0; i < 65536; ++i) {
        x = i % 256;
        y = 255 - i / 256;
        dest[(64 * x) + (y >> 2)] |= (uint32_t) (src[i] << (8 * (3 - (y % 4))));
    }

    return 0;
}

/**
 * Load pixel configuration from a BPC file (in Pixet format).
 * @param px_config Target configuration matrix.
 * @param file_path BPC file path.
 * @return Error code.
 */
int
katherine_px_config_load_bpc_file(katherine_px_config_t *px_config, const char *file_path)
{
    int res = 0;

    const size_t expected_size = sizeof(katherine_bpc_t);
    FILE *file                 = fopen(file_path, "rb");
    if (file == NULL) {
        res = -(int) map_fopen_errno(errno);
        goto err_fopen;
    }

    katherine_bpc_t *buffer = (katherine_bpc_t *) malloc(expected_size);
    if (buffer == NULL) {
        res = -KATHERINE_E_NOMEM;
        goto err_buffer;
    }

    const size_t actual_size = fread(buffer, 1, expected_size, file);
    if (expected_size != actual_size) {
        res = -KATHERINE_E_IO;
        goto err_fread;
    }

    res = katherine_px_config_load_bpc_data(px_config, buffer);

err_fread:
    free(buffer);
err_buffer:
    fclose(file);
err_fopen:
    return res;
}

/**
 * Load pixel configuration from a BPC file contents (in Pixet format).
 * @param px_config Target configuration matrix.
 * @param bpc BPC file data.
 * @return Error code.
 */
int
katherine_px_config_load_bpc_data(katherine_px_config_t *px_config, const katherine_bpc_t *bpc)
{
    // Reset the pixel values.
    memset(&px_config->words, 0, 65536);

    int x, y;
    uint32_t *dest                       = (uint32_t *) px_config->words;
    const unsigned char *src             = (const unsigned char *) bpc->px_config;
    static unsigned char reverse_array[] = {0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15};
    unsigned char val;

    for (int i = 0; i < 65536; ++i) {
        y   = i / 256;
        x   = i % 256;
        val = (unsigned char) ((src[i] & 0x21) | (reverse_array[((src[i] & 0x1E) >> 1)] << 1));

        y = 255 - y;
        dest[(64 * x) + (y >> 2)] |= (uint32_t) (val << (8 * (3 - (y % 4))));
    }

    return 0;
}

/* The helpers below locate the configuration byte of a single pixel in the
   packed matrix. Pixel coordinates follow the loaders' convention: x is the
   column, y is the row, and hits reported during acquisition carry the same
   coordinates. */

static inline uint8_t
_px_config_get_byte(const katherine_px_config_t *px_config, katherine_coord_t coord)
{
    const int yy = 255 - coord.y;
    return (uint8_t) (px_config->words[(64 * coord.x) + (yy >> 2)] >> (8 * (3 - (yy % 4))));
}

static inline void
_px_config_set_byte(katherine_px_config_t *px_config, katherine_coord_t coord, uint8_t byte)
{
    const int yy    = 255 - coord.y;
    const int shift = 8 * (3 - (yy % 4));
    uint32_t *word  = &px_config->words[(64 * coord.x) + (yy >> 2)];

    *word = (*word & ~((uint32_t) 0xFF << shift)) | ((uint32_t) byte << shift);
}

/**
 * Set the test bit of a single pixel.
 * Pixels with the test bit set receive test pulses during acquisition
 * (see katherine_set_test_pulses).
 * @param px_config Configuration matrix to modify.
 * @param coord Pixel coordinates.
 * @param enabled New value of the test bit.
 */
void
katherine_px_config_set_test_bit(katherine_px_config_t *px_config, katherine_coord_t coord, bool enabled)
{
    const uint8_t byte = _px_config_get_byte(px_config, coord);
    _px_config_set_byte(px_config, coord, (uint8_t) INSERT(byte, bmc_px, test, enabled));
}

/**
 * Get the test bit of a single pixel.
 * @param px_config Configuration matrix to inspect.
 * @param coord Pixel coordinates.
 * @return Current value of the test bit.
 */
bool
katherine_px_config_get_test_bit(const katherine_px_config_t *px_config, katherine_coord_t coord)
{
    return EXTRACT(_px_config_get_byte(px_config, coord), bmc_px, test);
}

/**
 * Set the mask bit of a single pixel.
 * Masked pixels do not report hits during acquisition.
 *
 * Polarity note: Tpx3 manual Table 23 describes this bit as "0: Disabled /
 * 1: Enabled", which reads as 1 = hits pass. The readout manual and the
 * vendor's own calibration tooling instead use 1 = masked, which is what
 * this accessor's `masked` parameter follows. Do not "fix" this against
 * Table 23; it would invert every caller's masking.
 *
 * @param px_config Configuration matrix to modify.
 * @param coord Pixel coordinates.
 * @param masked New value of the mask bit.
 */
void
katherine_px_config_set_mask_bit(katherine_px_config_t *px_config, katherine_coord_t coord, bool masked)
{
    const uint8_t byte = _px_config_get_byte(px_config, coord);
    _px_config_set_byte(px_config, coord, (uint8_t) INSERT(byte, bmc_px, mask, masked));
}

/**
 * Get the mask bit of a single pixel.
 * @param px_config Configuration matrix to inspect.
 * @param coord Pixel coordinates.
 * @return Current value of the mask bit.
 */
bool
katherine_px_config_get_mask_bit(const katherine_px_config_t *px_config, katherine_coord_t coord)
{
    return EXTRACT(_px_config_get_byte(px_config, coord), bmc_px, mask);
}

/**
 * Set the local threshold adjustment of a single pixel.
 *
 * Bit-order note: Tpx3 manual Table 23 stores the chip's Thr[3:0] nibble
 * bit-reversed within PCR[4:1] (PCR[4] = Thr[0], ..., PCR[1] = Thr[3]). This
 * accessor inserts loc_thl unreversed, so the value written here is in raw
 * PCR order, not chip DAC-value order -- e.g. set_loc_thl(1) programs a trim
 * of 8, not 1. The BPC loader (katherine_px_config_load_bpc_data() above)
 * applies that reversal and is therefore the one of this pair in the right
 * order; the two disagreeing with each other is an adjudicated defect,
 * fixed in 2.0. Until then, callers of this function who want the
 * DAC-value semantics must reverse the nibble themselves, e.g. with a
 * bitreverse4(value) helper.
 *
 * @param px_config Configuration matrix to modify.
 * @param coord Pixel coordinates.
 * @param loc_thl New threshold adjustment DAC value, 0 to 15.
 */
void
katherine_px_config_set_loc_thl(katherine_px_config_t *px_config, katherine_coord_t coord, uint8_t loc_thl)
{
    assert(loc_thl <= 15);

    const uint8_t byte = _px_config_get_byte(px_config, coord);
    _px_config_set_byte(px_config, coord, (uint8_t) INSERT(byte, bmc_px, loc_thl, loc_thl));
}

/**
 * Get the local threshold adjustment of a single pixel.
 *
 * Returns the raw PCR nibble, unreversed; see the bit-order note on
 * katherine_px_config_set_loc_thl() above.
 *
 * @param px_config Configuration matrix to inspect.
 * @param coord Pixel coordinates.
 * @return Current threshold adjustment DAC value, 0 to 15.
 */
uint8_t
katherine_px_config_get_loc_thl(const katherine_px_config_t *px_config, katherine_coord_t coord)
{
    return EXTRACT(_px_config_get_byte(px_config, coord), bmc_px, loc_thl);
}

#undef _BITS_bmc_px_mask_start
#undef _BITS_bmc_px_mask_mask
#undef _BITS_bmc_px_mask_type
#undef _BITS_bmc_px_loc_thl_start
#undef _BITS_bmc_px_loc_thl_mask
#undef _BITS_bmc_px_loc_thl_type
#undef _BITS_bmc_px_test_start
#undef _BITS_bmc_px_test_mask
#undef _BITS_bmc_px_test_type
