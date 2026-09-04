/**
 * \file
 * \brief Per-double-column clock phase correction.
 *
 * The pixel clock reaches the double columns in staggered phases, so a hit in
 * a later-phased column is latched against a later edge and its timestamp
 * comes out smaller. The decoder adds the column's offset back.
 *
 * These tests drive the DECODER and the INVERSE over a table the fixture below
 * fills, so what they check is that those two agree with each other and with
 * the stated sign -- not that the library builds the table correctly. They
 * cannot check that: the closed form is static in acquisition.c and the fill
 * happens inside katherine_acquisition_begin(), which needs a readout.
 *
 * The library's own table is checked entry by entry in test_e2e_acq.c, against
 * a formula written out there. Both halves are needed: without that one, an
 * assertion here that the accessor returns what the fixture stored would be a
 * tautology and would pass against a table that paired the wrong columns.
 *
 * The sign was measured on hardware rather than taken from a document -- see
 * misc/phase-evidence-2026-08-28/ -- and is the opposite of what the reference
 * implementation applies, so it is asserted explicitly and by direction rather
 * than only by magnitude.
 *
 * \author Petr Mánek
 * \date 30.8.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <katherine/katherine.h>

#include "protocol/md.h"

#include "ktest.h"

// Second implementation of the offset rule, written from the description
// rather than from the library's code: double column i is on phase i mod n,
// each phase one nth of a coarse tick later than the last.
static uint8_t
expected_offset(katherine_tpx3_freq_t freq, katherine_tpx3_phase_t phase, uint8_t x)
{
    const uint8_t n = katherine_actual_phases(freq, phase);
    if (n <= 1) return 0;

    const unsigned coarse = katherine_tpx3_toa_coarse_tick_to_fine_ticks(freq);
    const unsigned dc     = (unsigned) x / 2u;

    return (uint8_t) ((dc % n) * (coarse / n));
}

// An acquisition as begin() would have left it, except that the offset table
// is filled from expected_offset() above.
static void
fixture(katherine_acquisition_t *acq, katherine_tpx3_freq_t freq, katherine_tpx3_phase_t phase, bool correct)
{
    memset(acq, 0, sizeof(*acq));
    acq->toa_coarse_tick_to_fine_shift = katherine_tpx3_toa_coarse_tick_to_fine_shift(freq);
    acq->phase_count                   = katherine_actual_phases(freq, phase);
    acq->last_toa_offset               = katherine_tpx3_toa_epoch_bias(acq->toa_coarse_tick_to_fine_shift);
    acq->phase_correction              = correct ? KATHERINE_PHASE_CORRECTION_SOFTWARE : KATHERINE_PHASE_CORRECTION_NONE;

    if (correct) {
        for (unsigned x = 0; x < KATHERINE_TPX3_MATRIX_WIDTH; ++x) {
            acq->phase_offsets[x] = expected_offset(freq, phase, (uint8_t) x);
        }
    }
}

static const katherine_tpx3_freq_t FREQS[]   = {KATHERINE_TPX3_FREQ_20_MHZ, KATHERINE_TPX3_FREQ_40_MHZ, KATHERINE_TPX3_FREQ_80_MHZ, KATHERINE_TPX3_FREQ_160_MHZ};
static const katherine_tpx3_phase_t PHASES[] = {KATHERINE_TPX3_PHASE_1, KATHERINE_TPX3_PHASE_2, KATHERINE_TPX3_PHASE_4, KATHERINE_TPX3_PHASE_8, KATHERINE_TPX3_PHASE_16};

// ------------------------------------------------------------------

// The offsets are whole fine ticks at every setting. That is what lets a
// timestamp stay an integer, and it holds only because the phase count is the
// CLAMPED one: dividing a coarse tick by the enumerator's own name would give
// a quarter of a fine tick at the shortest divider.
static void
test_offsets_are_whole_fine_ticks(void)
{
    unsigned staggered = 0;

    for (size_t f = 0; f < 4; ++f) {
        for (size_t p = 0; p < 5; ++p) {
            const uint8_t n       = katherine_actual_phases(FREQS[f], PHASES[p]);
            const unsigned coarse = katherine_tpx3_toa_coarse_tick_to_fine_ticks(FREQS[f]);

            KT_REQUIRE(n >= 1);
            KT_CHECK_EQ(coarse % n, 0u);
            if (n > 1) ++staggered;
        }
    }

    // Thirteen of the twenty settings actually stagger; if that ever became
    // zero this test would pass while checking nothing.
    KT_CHECK_EQ(staggered, 13u);
}

static void
test_accessor_reports_the_applied_offset(void)
{
    katherine_acquisition_t acq;

    for (size_t f = 0; f < 4; ++f) {
        for (size_t p = 0; p < 5; ++p) {
            fixture(&acq, FREQS[f], PHASES[p], true);

            for (unsigned x = 0; x < KATHERINE_TPX3_MATRIX_WIDTH; ++x) {
                const katherine_coord_t co = {(uint8_t) x, 128};
                KT_CHECK_EQ(katherine_acquisition_timestamp_phase_offset(&acq, co),
                    expected_offset(FREQS[f], PHASES[p], (uint8_t) x));
            }
        }
    }
}

// Both halves of a double column share a clock phase, so the offsets come in
// equal pairs. Measured on hardware: the partner column matched in every row
// of every run.
static void
test_double_column_halves_share_a_phase(void)
{
    katherine_acquisition_t acq;
    fixture(&acq, KATHERINE_TPX3_FREQ_40_MHZ, KATHERINE_TPX3_PHASE_16, true);

    for (unsigned dc = 0; dc < KATHERINE_TPX3_MATRIX_WIDTH / 2; ++dc) {
        const katherine_coord_t even = {(uint8_t) (2 * dc), 0};
        const katherine_coord_t odd  = {(uint8_t) (2 * dc + 1), 0};
        KT_CHECK_EQ(katherine_acquisition_timestamp_phase_offset(&acq, even),
            katherine_acquisition_timestamp_phase_offset(&acq, odd));
    }
}

// With correction off nothing is applied, whatever the phase setting.
static void
test_no_offset_without_correction(void)
{
    katherine_acquisition_t acq;

    for (size_t p = 0; p < 5; ++p) {
        fixture(&acq, KATHERINE_TPX3_FREQ_40_MHZ, PHASES[p], false);

        for (unsigned x = 0; x < KATHERINE_TPX3_MATRIX_WIDTH; ++x) {
            const katherine_coord_t co = {(uint8_t) x, 0};
            KT_CHECK_EQ(katherine_acquisition_timestamp_phase_offset(&acq, co), 0u);
        }
    }
}

// ------------------------------------------------------------------
// What the decoder does with them.

static uint64_t
decode_one(const katherine_acquisition_t *acq, uint8_t x, uint16_t coarse, uint8_t ftoa)
{
    uint64_t md = 0;
    md          = INSERT(md, pmd_f_toa_tot, coord_x, (uint64_t) x);
    md          = INSERT(md, pmd_f_toa_tot, coord_y, (uint64_t) 128);
    md          = INSERT(md, pmd_f_toa_tot, toa, (uint64_t) coarse);
    md          = INSERT(md, pmd_f_toa_tot, ftoa, (uint64_t) ftoa);

    katherine_px_f_toa_tot_t dst;
    memset(&dst, 0, sizeof(dst));
    pmd_f_toa_tot_s4_map(&dst, &md, acq);
    return dst.timestamp;
}

// The direction, which is the claim the hardware settled and which no document
// would have given us. A later-phased column reports a SMALLER raw timestamp,
// so correction must make it LARGER -- the offset is added. Subtracting, as
// the reference implementation does, would move it the wrong way and double
// the stagger instead of removing it.
static void
test_correction_adds(void)
{
    katherine_acquisition_t on, off;
    fixture(&on, KATHERINE_TPX3_FREQ_40_MHZ, KATHERINE_TPX3_PHASE_16, true);
    fixture(&off, KATHERINE_TPX3_FREQ_40_MHZ, KATHERINE_TPX3_PHASE_16, false);

    unsigned compared = 0;

    for (unsigned dc = 1; dc < 16; ++dc) {
        const uint8_t x   = (uint8_t) (2 * dc);
        const uint8_t phi = expected_offset(KATHERINE_TPX3_FREQ_40_MHZ, KATHERINE_TPX3_PHASE_16, x);
        KT_REQUIRE(phi > 0);

        const uint64_t raw       = decode_one(&off, x, 1000, 3);
        const uint64_t corrected = decode_one(&on, x, 1000, 3);

        KT_CHECK(corrected > raw);
        KT_CHECK_EQ(corrected - raw, phi);
        ++compared;
    }

    KT_CHECK_EQ(compared, 15u);
}

// And what the correction is for: hits that arrived together come out
// together. Pixels in different double columns latch against edges staggered
// by exactly the offsets, so a simultaneous arrival appears in the raw stream
// as a coarse/fine pair that differs per column -- and correction must
// collapse those to one timestamp. This is the same statement the hardware
// test makes with test pulses.
static void
test_simultaneous_hits_come_out_equal(void)
{
    katherine_acquisition_t on;
    fixture(&on, KATHERINE_TPX3_FREQ_40_MHZ, KATHERINE_TPX3_PHASE_16, true);

    uint64_t first = 0;

    for (unsigned dc = 0; dc < 16; ++dc) {
        const uint8_t x   = (uint8_t) (2 * dc);
        const uint8_t phi = expected_offset(KATHERINE_TPX3_FREQ_40_MHZ, KATHERINE_TPX3_PHASE_16, x);

        // A later-phased column sees the same arrival as a smaller value, by
        // exactly its offset, which the fine counter absorbs. The fine field
        // holds four bits, so the offset itself is the largest fine value that
        // can stand in for it -- anything added on top would be masked away by
        // INSERT and would quietly make this test construct a different
        // arrival instead of the same one.
        KT_REQUIRE(phi <= 15);
        const uint64_t t = decode_one(&on, x, 1000, phi);

        if (dc == 0) first = t;
        KT_CHECK_EQ(t, first);
    }
}

// ------------------------------------------------------------------

// Recovery has to undo the offset, or the residue the fine term is read from
// is corrupted -- a phase offset is not a whole coarse tick.
static void
test_recovery_undoes_correction(void)
{
    katherine_acquisition_t on;
    fixture(&on, KATHERINE_TPX3_FREQ_40_MHZ, KATHERINE_TPX3_PHASE_16, true);

    unsigned checked = 0;

    for (unsigned dc = 0; dc < 16; ++dc) {
        const uint8_t x            = (uint8_t) (2 * dc);
        const katherine_coord_t co = {x, 128};
        const uint64_t t           = decode_one(&on, x, 4321, 9);
        const uint8_t phi          = katherine_acquisition_timestamp_phase_offset(&on, co);

        uint64_t toa = 0;
        uint8_t ftoa = 0;
        katherine_tpx3_timestamp_to_toa_ftoa(on.toa_coarse_tick_to_fine_shift, phi, t, &toa, &ftoa);

        KT_CHECK_EQ(toa, 4321u);
        KT_CHECK_EQ(ftoa, 9u);
        ++checked;

        // Forgetting the offset is wrong wherever there is one to forget.
        if (phi != 0) {
            katherine_tpx3_timestamp_to_toa_ftoa(on.toa_coarse_tick_to_fine_shift, 0, t, &toa, &ftoa);
            KT_CHECK(toa != 4321u || ftoa != 9u);
        }
    }

    KT_CHECK_EQ(checked, 16u);
}

int
main(void)
{
    KT_RUN(test_offsets_are_whole_fine_ticks);
    KT_RUN(test_accessor_reports_the_applied_offset);
    KT_RUN(test_double_column_halves_share_a_phase);
    KT_RUN(test_no_offset_without_correction);
    KT_RUN(test_correction_adds);
    KT_RUN(test_simultaneous_hits_come_out_equal);
    KT_RUN(test_recovery_undoes_correction);
    return kt_summary();
}
