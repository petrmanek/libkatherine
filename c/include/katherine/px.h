/**
 * @file
 * @brief Pixel data types for all supported acquisition modes.
 * @author Petr Mánek
 * @date 28.2.19
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <katherine/global.h>

/**
 * @addtogroup c_api
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct katherine_coord {
    uint8_t x;
    uint8_t y;
} katherine_coord_t;

KATHERINE_EXPORTED int
katherine_coord_snprint(char *buf, size_t cap, const katherine_coord_t *v);

typedef struct katherine_px_f_toa_tot {
    katherine_coord_t coord;
    uint8_t ftoa;
    uint64_t toa;
    uint16_t tot;
} katherine_px_f_toa_tot_t;

KATHERINE_EXPORTED int
katherine_px_f_toa_tot_snprint(char *buf, size_t cap, const katherine_px_f_toa_tot_t *v);

typedef struct katherine_px_toa_tot {
    katherine_coord_t coord;
    uint64_t toa;
    uint8_t hit_count;
    uint16_t tot;
} katherine_px_toa_tot_t;

KATHERINE_EXPORTED int
katherine_px_toa_tot_snprint(char *buf, size_t cap, const katherine_px_toa_tot_t *v);

typedef struct katherine_px_f_toa_only {
    katherine_coord_t coord;
    uint8_t ftoa;
    uint64_t toa;
} katherine_px_f_toa_only_t;

KATHERINE_EXPORTED int
katherine_px_f_toa_only_snprint(char *buf, size_t cap, const katherine_px_f_toa_only_t *v);

typedef struct katherine_px_toa_only {
    katherine_coord_t coord;
    uint64_t toa;
    uint8_t hit_count;
} katherine_px_toa_only_t;

KATHERINE_EXPORTED int
katherine_px_toa_only_snprint(char *buf, size_t cap, const katherine_px_toa_only_t *v);

typedef struct katherine_px_f_event_itot {
    katherine_coord_t coord;
    uint8_t hit_count;
    uint16_t event_count;
    uint16_t integral_tot;
} katherine_px_f_event_itot_t;

KATHERINE_EXPORTED int
katherine_px_f_event_itot_snprint(char *buf, size_t cap, const katherine_px_f_event_itot_t *v);

typedef struct katherine_px_event_itot {
    katherine_coord_t coord;
    uint16_t event_count;
    uint16_t integral_tot;
} katherine_px_event_itot_t;

KATHERINE_EXPORTED int
katherine_px_event_itot_snprint(char *buf, size_t cap, const katherine_px_event_itot_t *v);

#ifdef __cplusplus
}
#endif

/** @} */
