/**
 * @file
 * @brief Pixel data types for all supported acquisition modes.
 * @author Petr Mánek
 * @date 28.2.19
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied
 * verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <katherine/global.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @addtogroup c_api
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Units and encoding of the counter fields below (Timepix3 manual v2.0
 * unless noted).
 *
 * toa: pixel-clock ticks, not a fixed 25 ns -- the clock divider (Table 17)
 * makes it 25/12.5/6.25 ns at 40/80/160 MHz (Select_ToA_Clk[11] = 0, Table
 * 18: the counter runs off the phase-shifted pixel-matrix clock, not the
 * system clock). katherine_frame_info_t.start_time/end_time (acquisition.h)
 * tick at a fixed 25 ns Clk40 instead (sec. 4.2.5.5), so the two compare
 * directly only at 40 MHz. The chip's counter is 14 bits and unambiguous
 * only within 16384 ticks; in sequential readout, the timestamp-offset datum
 * that extends it is never sent (sec. 2.1.4; readout manual sec. 2.4), so toa
 * wraps every 16384 ticks despite the 64-bit field.
 *
 * tot, hit_count, event_count, integral_tot: chip counters, encoded on the
 * sensor as LFSR states (Table 3) the same way toa is Gray-coded. Whether
 * the readout firmware decodes these before the value reaches the host is
 * believed true but not yet confirmed on hardware -- treat them as raw
 * counter states until it is. Saturation values (Table 4): tot and
 * integral_tot at 1022, ftoa at 15, the 4-bit hit_count at 14 (not 15).
 */

typedef struct katherine_coord {
  uint8_t x;
  uint8_t y;
} katherine_coord_t;

KATHERINE_EXPORTED int katherine_coord_snprint(char *buf, size_t cap,
                                               const katherine_coord_t *v);

typedef struct katherine_px_f_toa_tot {
  katherine_coord_t coord;
  uint8_t
      ftoa; ///< Fast ToA, binary counter (Table 3); saturates at 15 (Table 4)
  uint64_t toa; ///< Pixel-clock ticks, Gray-coded on the chip; see the file
                ///< header for units and wrap behavior
  uint16_t tot; ///< Chip LFSR counter state; see the file header
} katherine_px_f_toa_tot_t;

KATHERINE_EXPORTED int
katherine_px_f_toa_tot_snprint(char *buf, size_t cap,
                               const katherine_px_f_toa_tot_t *v);

typedef struct katherine_px_toa_tot {
  katherine_coord_t coord;
  uint64_t toa; ///< Pixel-clock ticks, Gray-coded on the chip; see the file
                ///< header for units and wrap behavior
  uint8_t hit_count; ///< Chip LFSR counter state; see the file header
  uint16_t tot;      ///< Chip LFSR counter state; see the file header
} katherine_px_toa_tot_t;

KATHERINE_EXPORTED int
katherine_px_toa_tot_snprint(char *buf, size_t cap,
                             const katherine_px_toa_tot_t *v);

typedef struct katherine_px_f_toa_only {
  katherine_coord_t coord;
  uint8_t
      ftoa; ///< Fast ToA, binary counter (Table 3); saturates at 15 (Table 4)
  uint64_t toa; ///< Pixel-clock ticks, Gray-coded on the chip; see the file
                ///< header for units and wrap behavior
} katherine_px_f_toa_only_t;

KATHERINE_EXPORTED int
katherine_px_f_toa_only_snprint(char *buf, size_t cap,
                                const katherine_px_f_toa_only_t *v);

typedef struct katherine_px_toa_only {
  katherine_coord_t coord;
  uint64_t toa; ///< Pixel-clock ticks, Gray-coded on the chip; see the file
                ///< header for units and wrap behavior
  uint8_t hit_count; ///< Chip LFSR counter state; see the file header
} katherine_px_toa_only_t;

KATHERINE_EXPORTED int
katherine_px_toa_only_snprint(char *buf, size_t cap,
                              const katherine_px_toa_only_t *v);

typedef struct katherine_px_f_event_itot {
  katherine_coord_t coord;
  uint8_t hit_count;     ///< Chip LFSR counter state; see the file header
  uint16_t event_count;  ///< Chip LFSR counter state; see the file header
  uint16_t integral_tot; ///< Chip LFSR counter state; see the file header
} katherine_px_f_event_itot_t;

KATHERINE_EXPORTED int
katherine_px_f_event_itot_snprint(char *buf, size_t cap,
                                  const katherine_px_f_event_itot_t *v);

typedef struct katherine_px_event_itot {
  katherine_coord_t coord;
  uint16_t event_count;  ///< Chip LFSR counter state; see the file header
  uint16_t integral_tot; ///< Chip LFSR counter state; see the file header
} katherine_px_event_itot_t;

KATHERINE_EXPORTED int
katherine_px_event_itot_snprint(char *buf, size_t cap,
                                const katherine_px_event_itot_t *v);

#ifdef __cplusplus
}
#endif

/** @} */
