/**
 * \file
 * \brief Timestamp units for Timepix3 pixel data.
 * \author Petr Mánek
 * \date 29.8.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdbool.h>

#include <katherine/toa.h>

// log2 of the number of fine-oscillator ticks in one coarse tick, indexed by
// katherine_tpx3_freq_t.
//
// The fine oscillator is fixed at 640 MHz while the pixel clock is the same
// PLL divided down, so every ratio is a whole number and a power of two. That
// is what lets a timestamp be carried as an integer count of fine ticks with
// no rounding at any frequency, and what makes the per-double-column phase
// step -- a coarse tick divided by the phase count -- a whole number of them
// as well.
//
// The shift is stored rather than the ratio, and the ratio derived from it, so
// the two cannot disagree. The decoder wants the shift: a ratio read from
// memory is not a compile-time constant, so multiplying by it emits a real
// multiply rather than the shift its value would allow.
static const uint8_t KATHERINE_TOA_FINE_SHIFT[4] = {
    /* KATHERINE_TPX3_FREQ_20_MHZ  */ 5, /* 32 fine ticks */
    /* KATHERINE_TPX3_FREQ_40_MHZ  */ 4, /* 16 */
    /* KATHERINE_TPX3_FREQ_80_MHZ  */ 3, /*  8 */
    /* KATHERINE_TPX3_FREQ_160_MHZ */ 2, /*  4 */
};

static bool
katherine_toa_freq_in_range(katherine_tpx3_freq_t freq)
{
    // Signed comparison on purpose: the parameter is an enumeration, and a
    // caller passing a negative value would otherwise index far outside the
    // table after conversion to an unsigned index.
    return (int) freq >= 0 && (int) freq <= KATHERINE_TPX3_FREQ_160_MHZ;
}

/**
 * Fine-oscillator ticks in one pixel-clock tick.
 *
 * The unit timestamps are carried in. Callers need this to reason about the
 * resolution of a timestamp, and to divide a coarse tick by a phase count.
 *
 * \see katherine_tpx3_toa_coarse_tick_to_fine_shift
 * \see katherine_actual_phases
 *
 * \param freq Pixel-clock frequency selector.
 * \return Fine ticks per coarse tick, or 0 if freq is out of range.
 */
uint8_t
katherine_tpx3_toa_coarse_tick_to_fine_ticks(katherine_tpx3_freq_t freq)
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
 * \see katherine_tpx3_toa_coarse_tick_to_fine_ticks
 *
 * \param freq Pixel-clock frequency selector.
 * \return Shift taking coarse ticks to fine ticks, or 0 if freq is out of range.
 */
uint8_t
katherine_tpx3_toa_coarse_tick_to_fine_shift(katherine_tpx3_freq_t freq)
{
    if (!katherine_toa_freq_in_range(freq)) return 0;

    return KATHERINE_TOA_FINE_SHIFT[freq];
}

// Fine-oscillator ticks in one second. The oscillator is fixed at 640 MHz, so
// this needs no configuration -- unlike the coarse tick, which the divider
// moves. Exactly 640e6, and a whole number, which is what makes the split
// below exact rather than merely close.
#define KATHERINE_TOA_FINE_TICKS_PER_SECOND 640000000u

// The epoch bias katherine_acquisition_begin() applies, as a shift. Larger of
// the coarse tick and the fine field's span; see the timestamp notes in px.h.
#define KATHERINE_TOA_FINE_SPAN_SHIFT       4u

static uint8_t
katherine_toa_epoch_bias_shift(uint8_t coarse_tick_to_fine_shift)
{
    return coarse_tick_to_fine_shift > KATHERINE_TOA_FINE_SPAN_SHIFT ? coarse_tick_to_fine_shift
                                                                     : KATHERINE_TOA_FINE_SPAN_SHIFT;
}

/**
 * The bias katherine_acquisition_begin() puts on the timestamp epoch.
 *
 * The decoder subtracts the chip's fine counter from its coarse one, and
 * biasing the epoch by at least the fine field's span makes that subtraction
 * unable to go below zero -- which spares the decode loop a per-hit test worth
 * 10-25% of its throughput, and avoids wrapping a first-tick hit to some 914
 * years in the future.
 *
 * One coarse tick does not suffice at the shorter dividers, where a coarse
 * tick is 4 or 8 fine ticks against a fine field spanning 16, so the bias is
 * the larger of the two. Both are powers of two, so the result stays a whole
 * multiple of the coarse tick -- which is what keeps the chip's fine counter
 * recoverable from a timestamp.
 *
 * Exposed so that the decoder and katherine_tpx3_timestamp_to_toa_ftoa() share
 * one definition rather than two that must agree, and so callers doing their
 * own arithmetic on timestamps can account for it.
 *
 * \see katherine_tpx3_timestamp_to_toa_ftoa
 *
 * \param coarse_tick_to_fine_shift Shift for the acquisition's pixel clock.
 * \return Bias in fine-oscillator ticks.
 */
uint64_t
katherine_tpx3_toa_epoch_bias(uint8_t coarse_tick_to_fine_shift)
{
    return (uint64_t) 1 << katherine_toa_epoch_bias_shift(coarse_tick_to_fine_shift);
}

/**
 * Convert a timestamp to whole seconds and the remainder within that second.
 *
 * Split rather than summed because no single double holds the range: the
 * hardware spans 81.4 days at 1.5625 ns, which needs about 15.7 significant
 * digits, and a product in nanoseconds stops being exact after roughly 6.5
 * days. Splitting removes the problem instead of bounding it. The quotient is
 * exact integer division, and the remainder is below 6.4e8, so scaling it by
 * 25/16 needs at most 34 bits of mantissa -- far inside a double. Every input
 * across the whole range therefore converts exactly, to a maximum of
 * 999999998.4375 ns.
 *
 * Summing the two parts back into one double reintroduces exactly the loss
 * this avoids.
 *
 * The epoch bias described at katherine_px_f_toa_tot_t::timestamp is NOT
 * removed here: it is a constant, differences are unaffected, and a timestamp
 * is frame-relative rather than absolute in any case. Use
 * katherine_tpx3_timestamp_to_toa_ftoa() to recover the chip's own quantities.
 *
 * \see katherine_tpx3_timestamp_to_toa_ftoa
 *
 * \param timestamp Timestamp in fine-oscillator ticks.
 * \param sec Whole seconds, or NULL.
 * \param nsec Remainder within that second, in nanoseconds, or NULL.
 */
void
katherine_tpx3_timestamp_to_seconds(uint64_t timestamp, uint64_t *sec, double *nsec)
{
    if (sec != NULL) *sec = timestamp / KATHERINE_TOA_FINE_TICKS_PER_SECOND;
    if (nsec != NULL)
        *nsec = (double) (timestamp % KATHERINE_TOA_FINE_TICKS_PER_SECOND) * 1.5625;
}

/**
 * Recover the chip's own time of arrival and fine time of arrival.
 *
 * The inverse of what the decoder does, giving back the two counters the
 * sensor reported: toa in coarse ticks since the acquisition epoch, ftoa in
 * fine ticks. Together they are the representation libkatherine 1.x delivered,
 * which makes this the direct route for code being ported.
 *
 * Recovery is exact at any point in an acquisition. The timestamp offset the
 * stream delivers is a whole multiple of the coarse tick and so vanishes under
 * the modulus, and the epoch bias is likewise a whole multiple and is removed
 * here. A phase offset is neither, so it must be undone before the modulus --
 * pass the offset that was applied to this pixel, or 0 when no phase
 * correction is in effect.
 *
 * \see katherine_tpx3_timestamp_to_seconds
 *
 * \param coarse_tick_to_fine_shift Shift for the acquisition's pixel clock.
 * \param phase_offset Phase offset applied to this pixel's column, in fine ticks.
 * \param timestamp Timestamp in fine-oscillator ticks.
 * \param toa Coarse ticks since the epoch, or NULL.
 * \param ftoa Fine ticks the hit preceded its coarse tick by, or NULL.
 */
void
katherine_tpx3_timestamp_to_toa_ftoa(uint8_t coarse_tick_to_fine_shift, uint8_t phase_offset,
    uint64_t timestamp, uint64_t *toa, uint8_t *ftoa)
{
    const uint8_t bias_shift = katherine_toa_epoch_bias_shift(coarse_tick_to_fine_shift);
    const uint64_t fine_mask = ((uint64_t) 1 << coarse_tick_to_fine_shift) - 1u;

    // The phase offset was added, so undoing it comes first: it is not a whole
    // coarse tick and would otherwise corrupt the residue carrying the fine
    // term. This cannot go below zero -- the same offset was added to this
    // very value.
    const uint64_t t = timestamp - phase_offset;

    // How far short of the next coarse tick the value falls, which is exactly
    // what was subtracted from it. Written as a subtraction rather than as a
    // negation of an unsigned value: the two are identical for a power-of-two
    // modulus, but MSVC rightly warns about the latter (C4146).
    const uint64_t fine = ((fine_mask + 1u) - (t & fine_mask)) & fine_mask;

    if (ftoa != NULL) *ftoa = (uint8_t) fine;
    if (toa != NULL)
        *toa = ((t + fine) >> coarse_tick_to_fine_shift) - ((uint64_t) 1 << (bias_shift - coarse_tick_to_fine_shift));
}
