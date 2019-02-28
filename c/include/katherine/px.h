/* Katherine Control Library
 *
 * This file was created on 28.2.19 by Petr Manek.
 * 
 * Contents of this file are copyrighted and subject to license
 * conditions specified in the LICENSE file located in the top
 * directory.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct katherine_coord {
    uint8_t x;
    uint8_t y;
} katherine_coord_t;

typedef struct katherine_px_f_toa_tot {
    katherine_coord_t coord;
    uint8_t ftoa;
    uint64_t toa;
    uint16_t tot;
} katherine_px_f_toa_tot_t;

typedef struct katherine_px_toa_tot {
    katherine_coord_t coord;
    uint64_t toa;
    uint8_t hit_count;
    uint16_t tot;
} katherine_px_toa_tot_t;

typedef struct katherine_px_f_toa_only {
    katherine_coord_t coord;
    uint8_t ftoa;
    uint64_t toa;
} katherine_px_f_toa_only_t;

typedef struct katherine_px_toa_only {
    katherine_coord_t coord;
    uint64_t toa;
    uint8_t hit_count;
} katherine_px_toa_only_t;

typedef struct katherine_px_f_event_itot {
    katherine_coord_t coord;
    uint8_t hit_count;
    uint16_t event_count;
    uint16_t integral_tot;
} katherine_px_f_event_itot_t;

typedef struct katherine_px_event_itot {
    katherine_coord_t coord;
    uint16_t event_count;
    uint16_t integral_tot;
} katherine_px_event_itot_t;

#ifdef __cplusplus
}
#endif
