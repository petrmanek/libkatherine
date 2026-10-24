/**
 * \file
 * \brief Timepix3's share of an acquisition's private state.
 * \author Petr Mánek
 * \date 24.10.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdalign.h>
#include <stdint.h>

#include <katherine/acquisition.h>
#include <katherine/px.h>

/**
 * The state the Timepix3 decoder keeps between hits.
 *
 * Lives in katherine_acquisition_t::priv, which is an opaque reserve of
 * fixed size, so that a later ASIC can keep something else there without
 * moving a public field or changing the library's ABI.
 */
typedef struct katherine_acquisition_tpx3_priv {
    /**
     * Timestamp offset in effect, in fine-oscillator ticks. Carries the epoch
     * bias described at katherine_tpx3_px_f_toa_tot_t::timestamp, so it is never
     * zero during an acquisition and is not the offset the stream delivered.
     */
    uint64_t last_toa_offset;

    /**
     * Shift taking a coarse tick to the fine ticks that make it up, resolved
     * by katherine_acquisition_begin(). The ratio itself is 1 << this.
     */
    uint8_t toa_coarse_tick_to_fine_shift;

    /**
     * Pixel-clock phases this configuration actually yields, which the clock
     * divider may clamp below what katherine_tpx3_phase_t asked for.
     */
    uint8_t phase_count;

    /**
     * Per-column phase offsets in fine-oscillator ticks, added by the decoder.
     * Filled only when phase_correction is SOFTWARE; all zeroes otherwise, so
     * the decoder needs no test for whether correction is in effect. Read it
     * through katherine_acquisition_timestamp_phase_offset().
     */
    uint8_t phase_offsets[KATHERINE_TPX3_MATRIX_WIDTH];
} katherine_acquisition_tpx3_priv_t;

// The reserve is sized for the largest private state any ASIC this library
// drives needs, and these say so rather than leaving it to arithmetic in a
// comment. Timepix4 does not stagger clock phases -- the reference's
// Readout4Settings::calculateToaPhaseCount() returns 1 -- so it needs no
// per-column table and cannot be the one that overflows this.
_Static_assert(sizeof(katherine_acquisition_tpx3_priv_t) <= KATHERINE_ACQUISITION_PRIV_SIZE,
    "the Timepix3 decoder state must fit katherine_acquisition_t::priv");
_Static_assert(alignof(katherine_acquisition_tpx3_priv_t) <= alignof(katherine_acquisition_priv_t),
    "katherine_acquisition_t::priv must be aligned at least as strictly as what it holds");

/** The Timepix3 decoder state of an acquisition. */
static inline katherine_acquisition_tpx3_priv_t *
katherine_acquisition_tpx3_priv(katherine_acquisition_t *acquisition)
{
    return (katherine_acquisition_tpx3_priv_t *) &acquisition->priv;
}

/** \copybrief katherine_acquisition_tpx3_priv() */
static inline const katherine_acquisition_tpx3_priv_t *
katherine_acquisition_tpx3_priv_const(const katherine_acquisition_t *acquisition)
{
    return (const katherine_acquisition_tpx3_priv_t *) &acquisition->priv;
}
