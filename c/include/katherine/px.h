/**
 * \file
 * \brief Pixel data types for all supported acquisition modes.
 * \author Petr Mánek
 * \date 28.2.19
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <katherine/global.h>

/**
 * \defgroup katherine_px Pixels
 * \ingroup katherine_c_api
 * \brief Decoded pixel types and the coordinate helpers over them.
 */

/**
 * \addtogroup katherine_px
 * \{
 */

#ifdef __cplusplus
extern "C" {
#endif

//
// Units and encoding of the counter fields below (Timepix3 manual v2.0
// unless noted).
//
// timestamp: fine-oscillator ticks of 1/640 MHz = 1.5625 ns, counted from an
// arbitrary epoch. Not the chip's ToA field: the chip reports a coarse counter
// running off the phase-shifted pixel-matrix clock (25/12.5/6.25 ns by the
// divider of Table 17; Select_ToA_Clk[11] = 0, Table 18) and, where fine time
// stamping is coherent, a 4-bit fine counter that measures how far the hit
// preceded its coarse tick. Both are combined into this one field at decode,
// which is why it is named for the quantity rather than for either counter.
// The fine oscillator is fixed at 640 MHz while the pixel clock is the same
// PLL divided down, so a coarse tick is always a whole number of fine ticks
// and nothing is rounded; katherine_tpx3_toa_coarse_tick_to_fine_ticks (toa.h)
// gives the ratio.
//
// One field rather than two so that the value is monotonic in arrival time on
// its own: ordering, differencing and clustering need no further arithmetic
// and cannot forget any. The chip's own quantities remain recoverable from it.
//
// The epoch is offset by the larger of one coarse tick and the fine field's
// span of 16 ticks -- so 50/25/25/25 ns by the frequency, one coarse tick only
// where a coarse tick is itself at least that wide. This is deliberate and must not be removed: the fine counter is
// subtracted from the coarse one, which would underflow for a hit in the very
// first coarse tick after an offset reset, and an unsigned wrap there produces
// a timestamp some 914 years in the future rather than a slightly early one.
// Biasing the epoch makes that unrepresentable at no per-hit cost.
//
// Differences between timestamps are therefore exact, and every absolute value
// carries the same constant offset. Two consequences worth knowing:
// katherine_tpx3_timestamp_to_toa_ftoa() removes the bias, so the chip's own
// counters come back exactly -- provided it is also given the phase offset
// applied to the pixel, from katherine_acquisition_timestamp_phase_offset().
// Passing zero where an offset was applied returns a wrong answer silently,
// since a phase offset is not a whole coarse tick and corrupts the residue the
// fine term is read from; and a timestamp converted to seconds sits one
// coarse tick later than katherine_frame_info_t.start_time would suggest,
// which at 40 MHz is exactly one of that field's own 25 ns ticks.
//
// katherine_frame_info_t.start_time/end_time (acquisition.h) tick at a fixed
// 25 ns Clk40 instead (sec. 4.2.5.5), so they are not in these units. The
// chip's coarse counter is 14 bits and unambiguous only within 16384 of its
// own ticks; in sequential readout the timestamp-offset datum that extends it
// is never sent (sec. 2.1.4; readout manual sec. 2.4), so the timestamp wraps
// on that period despite the 64-bit field.
//
// tot, hit_count, event_count, integral_tot: chip counters, encoded on the
// sensor as LFSR states (Table 3) the same way toa is Gray-coded. The readout
// DECODES all four before they reach the host, so every one may be read as the
// quantity it names rather than as a counter state. Measured on a Gen1 readout
// with test pulses, where the injected charge and the number of pulses are both
// chosen:
// event_count reads exactly the pulse count (1, 3, 7, 20 and 100 reproduced);
// hit_count likewise, up to its saturation;
// integral_tot is linear in the pulse count at fixed amplitude, 17.0 per
// pulse across a twentyfold range;
// tot is linear in amplitude, about 0.050 per mV from 110 mV to 610 mV.
// The last two agree with each other, integral_tot for a single pulse matching
// tot for the same pulse, which is what an integral of ToT should do.
// Saturation values (Table 4): tot and integral_tot at 1022, the 4-bit
// hit_count at 14 (not 15). The chip's fine counter saturates at 15, which
// the combination above consumes rather than reports.

/// Columns in the Timepix3 pixel matrix, and so rows: it is square.
#define KATHERINE_TPX3_MATRIX_WIDTH 256

typedef struct katherine_coord {
    uint8_t x;
    uint8_t y;
} katherine_coord_t;

KATHERINE_EXPORTED int
katherine_coord_snprint(char *buf, size_t cap, const katherine_coord_t *v);

typedef struct katherine_px_f_toa_tot {
    katherine_coord_t coord;
    uint64_t timestamp; ///< Fine-oscillator ticks; see the file header
    uint16_t tot;       ///< Decoded time over threshold; see the file header
} katherine_px_f_toa_tot_t;

KATHERINE_EXPORTED int
katherine_px_f_toa_tot_snprint(char *buf, size_t cap, const katherine_px_f_toa_tot_t *v);

typedef struct katherine_px_toa_tot {
    katherine_coord_t coord;
    uint64_t timestamp; ///< Fine-oscillator ticks; see the file header
    uint8_t hit_count;  ///< Decoded pixel hit counter, saturating at 14 (Table 4)
    uint16_t tot;       ///< Decoded time over threshold; see the file header
} katherine_px_toa_tot_t;

KATHERINE_EXPORTED int
katherine_px_toa_tot_snprint(char *buf, size_t cap, const katherine_px_toa_tot_t *v);

typedef struct katherine_px_f_toa_only {
    katherine_coord_t coord;
    uint64_t timestamp; ///< Fine-oscillator ticks; see the file header
} katherine_px_f_toa_only_t;

KATHERINE_EXPORTED int
katherine_px_f_toa_only_snprint(char *buf, size_t cap, const katherine_px_f_toa_only_t *v);

typedef struct katherine_px_toa_only {
    katherine_coord_t coord;
    uint64_t timestamp; ///< Fine-oscillator ticks; see the file header
    uint8_t hit_count;  ///< Decoded pixel hit counter, saturating at 14 (Table 4)
} katherine_px_toa_only_t;

KATHERINE_EXPORTED int
katherine_px_toa_only_snprint(char *buf, size_t cap, const katherine_px_toa_only_t *v);

// No hit counter: with the fast oscillator on, bits [3:0] of an Event+iToT
// word are dummy (Tpx3 manual Figure 1, p8) -- this is the one mode where the
// fast variant carries less than the slow one, there being no fine ToA to
// report. Confirmed on a Gen1 readout: over 3012 pixels at high occupancy the
// field read zero throughout while the event counter saturated.
typedef struct katherine_px_f_event_itot {
    katherine_coord_t coord;
    uint16_t event_count;  ///< Decoded event count; see the file header
    uint16_t integral_tot; ///< Decoded integral of time over threshold; see the file header
} katherine_px_f_event_itot_t;

KATHERINE_EXPORTED int
katherine_px_f_event_itot_snprint(char *buf, size_t cap, const katherine_px_f_event_itot_t *v);

typedef struct katherine_px_event_itot {
    katherine_coord_t coord;
    uint8_t hit_count;     ///< Decoded pixel hit counter, saturating at 14 (Table 4)
    uint16_t event_count;  ///< Decoded event count; see the file header
    uint16_t integral_tot; ///< Decoded integral of time over threshold; see the file header
} katherine_px_event_itot_t;

KATHERINE_EXPORTED int
katherine_px_event_itot_snprint(char *buf, size_t cap, const katherine_px_event_itot_t *v);

#ifdef __cplusplus
}
#endif

/** \} */
