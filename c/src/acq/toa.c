/**
 * @file
 * @brief Timestamp units for Timepix3 pixel data.
 * @author Petr Mánek
 * @date 29.8.26
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdbool.h>

#include <katherine/toa.h>

/* log2 of the number of fine-oscillator ticks in one coarse tick, indexed by
 * katherine_freq_t.
 *
 * The fine oscillator is fixed at 640 MHz while the pixel clock is the same
 * PLL divided down, so every ratio is a whole number and a power of two. That
 * is what lets a timestamp be carried as an integer count of fine ticks with
 * no rounding at any frequency, and what makes the per-double-column phase
 * step -- a coarse tick divided by the phase count -- a whole number of them
 * as well.
 *
 * The shift is stored rather than the ratio, and the ratio derived from it, so
 * the two cannot disagree. The decoder wants the shift: a ratio read from
 * memory is not a compile-time constant, so multiplying by it emits a real
 * multiply rather than the shift its value would allow. */
static const uint8_t KATHERINE_TOA_FINE_SHIFT[4] = {
    /* FREQ_20  */ 5, /* 32 fine ticks */
    /* FREQ_40  */ 4, /* 16 */
    /* FREQ_80  */ 3, /*  8 */
    /* FREQ_160 */ 2, /*  4 */
};

static bool
katherine_toa_freq_in_range(katherine_freq_t freq)
{
    /* Signed comparison on purpose: the parameter is an enumeration, and a
       caller passing a negative value would otherwise index far outside the
       table after conversion to an unsigned index. */
    return (int) freq >= 0 && (int) freq <= FREQ_160;
}

/**
 * Fine-oscillator ticks in one pixel-clock tick.
 *
 * The unit timestamps are carried in. Callers need this to reason about the
 * resolution of a timestamp, and to divide a coarse tick by a phase count.
 *
 * @see katherine_tpx3_toa_coarse_tick_to_fine_shift
 * @see katherine_actual_phases
 *
 * @param freq Pixel-clock frequency selector.
 * @return Fine ticks per coarse tick, or 0 if freq is out of range.
 */
uint8_t
katherine_tpx3_toa_coarse_tick_to_fine_ticks(katherine_freq_t freq)
{
    if (!katherine_toa_freq_in_range(freq)) return 0;

    return (uint8_t) (1u << KATHERINE_TOA_FINE_SHIFT[freq]);
}

/**
 * The same ratio as a shift.
 *
 * Every ratio is a power of two, so scaling a coarse count into fine ticks is
 * a shift. Worth having as its own accessor because the distinction is not
 * cosmetic in a decode loop: a ratio held in a variable is opaque to the
 * compiler, which must emit a multiply for it, while the shift costs one
 * cheap operation. katherine_acquisition_begin() caches this for the decoder.
 *
 * @see katherine_tpx3_toa_coarse_tick_to_fine_ticks
 *
 * @param freq Pixel-clock frequency selector.
 * @return Shift taking coarse ticks to fine ticks, or 0 if freq is out of range.
 */
uint8_t
katherine_tpx3_toa_coarse_tick_to_fine_shift(katherine_freq_t freq)
{
    if (!katherine_toa_freq_in_range(freq)) return 0;

    return KATHERINE_TOA_FINE_SHIFT[freq];
}
