/**
 * \file
 * \brief Timestamp conversion and recovery.
 *
 * Pure functions over a timestamp, so none of this needs a readout: the values
 * are constructed the way the decoder would have built them and put back
 * through the inverse.
 *
 * The recovery tests are written against an independently computed forward
 * direction rather than against the decoder, so that a change of convention in
 * one has to be made deliberately in the other rather than cancelling out
 * silently.
 *
 * \author Petr Mánek
 * \date 29.8.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdbool.h>
#include <stdint.h>

#include <katherine/katherine.h>

#include "ktest.h"

#define FINE_TICKS_PER_SECOND 640000000ull
#define FINE_TICK_NS          1.5625

// What the decoder produces, written out here rather than reused from md.h so
// that recovery is checked against the convention and not against the code.
static uint64_t
forward(uint8_t shift, uint64_t coarse, uint8_t ftoa, uint8_t phase_offset)
{
    return (coarse << shift) + katherine_tpx3_toa_epoch_bias(shift) - ftoa + phase_offset;
}

// ------------------------------------------------------------------
// Seconds.

static void
test_seconds_splits_exactly(void)
{
    uint64_t sec = 0;
    double nsec  = 0.0;

    katherine_tpx3_timestamp_to_seconds(0, &sec, &nsec);
    KT_CHECK_EQ(sec, 0u);
    KT_CHECK_EXACT(nsec, 0.0);

    // One tick is the resolution, and asking for it is how a caller learns
    // that number without the library exporting a constant to open-code.
    katherine_tpx3_timestamp_to_seconds(1, &sec, &nsec);
    KT_CHECK_EQ(sec, 0u);
    KT_CHECK_EXACT(nsec, FINE_TICK_NS);

    // Either side of a second boundary.
    katherine_tpx3_timestamp_to_seconds(FINE_TICKS_PER_SECOND - 1, &sec, &nsec);
    KT_CHECK_EQ(sec, 0u);
    KT_CHECK_EXACT(nsec, 999999998.4375);

    katherine_tpx3_timestamp_to_seconds(FINE_TICKS_PER_SECOND, &sec, &nsec);
    KT_CHECK_EQ(sec, 1u);
    KT_CHECK_EXACT(nsec, 0.0);

    katherine_tpx3_timestamp_to_seconds(FINE_TICKS_PER_SECOND + 1, &sec, &nsec);
    KT_CHECK_EQ(sec, 1u);
    KT_CHECK_EXACT(nsec, FINE_TICK_NS);
}

static void
test_seconds_exact_at_the_hardware_limit(void)
{
    // 81.4 days, the span of the readout's timer, is where a single double in
    // nanoseconds would have lost the low bits long ago. The split does not:
    // the fraction never exceeds one second, so it never runs out of mantissa
    // however far into a run the hit falls.
    const uint64_t ticks = 4500000000000000ull;

    uint64_t sec = 0;
    double nsec  = 0.0;
    katherine_tpx3_timestamp_to_seconds(ticks, &sec, &nsec);

    KT_CHECK_EQ(sec, ticks / FINE_TICKS_PER_SECOND);
    KT_CHECK(nsec >= 0.0 && nsec < 1e9);

    // Exact, not merely close: the remainder scaled by 25/16 is representable,
    // so this is one of the few places a bit-for-bit comparison is the claim
    // rather than a hazard.
    const uint64_t rem = ticks % FINE_TICKS_PER_SECOND;
    KT_CHECK_EXACT(nsec, (double) rem * FINE_TICK_NS);

    // Round-trip. Dividing back is not exact in general, so this one is a
    // tolerance check -- truncating it to an integer would turn a last-bit
    // rounding into an off-by-one and fail for reasons that say nothing about
    // the conversion.
    KT_CHECK_CLOSE(nsec / FINE_TICK_NS, (double) rem);
}

static void
test_seconds_accepts_null_outputs(void)
{
    uint64_t sec = 12345;
    double nsec  = 6789.0;

    katherine_tpx3_timestamp_to_seconds(FINE_TICKS_PER_SECOND, &sec, NULL);
    KT_CHECK_EQ(sec, 1u);
    KT_CHECK_EXACT(nsec, 6789.0); /* untouched sentinel */

    katherine_tpx3_timestamp_to_seconds(FINE_TICKS_PER_SECOND, NULL, &nsec);
    KT_CHECK_EXACT(nsec, 0.0);
}

// ------------------------------------------------------------------
// Recovery.

// Coarse-only modes carry no fine term, and those run at every divider.
static void
test_recovers_coarse_at_every_divider(void)
{
    for (uint8_t shift = 2; shift <= 5; ++shift) {
        for (uint64_t coarse = 0; coarse < 40; ++coarse) {
            uint64_t toa = 0;
            uint8_t ftoa = 1;
            katherine_tpx3_timestamp_to_toa_ftoa(shift, 0, forward(shift, coarse, 0, 0), &toa, &ftoa);
            KT_CHECK_EQ(toa, coarse);
            KT_CHECK_EQ(ftoa, 0u);
        }
    }
}

// With a fine term, recovery is exact only where a coarse tick holds the whole
// 16-tick field. That is the same condition katherine_freq_is_fast_vco_supported()
// reports, and katherine_acquisition_begin() refuses to produce a fine term
// anywhere else -- so the excluded dividers are unreachable rather than merely
// untested. Asserted here so the boundary is recorded, not assumed.
static void
test_recovers_the_fine_term_where_it_is_meaningful(void)
{
    unsigned covered = 0;

    for (uint8_t shift = 2; shift <= 5; ++shift) {
        if (((uint64_t) 1 << shift) <= 15) continue;
        ++covered;

        for (uint64_t coarse = 0; coarse < 40; ++coarse) {
            for (uint8_t f = 0; f <= 15; ++f) {
                uint64_t toa = 0;
                uint8_t ftoa = 0;
                katherine_tpx3_timestamp_to_toa_ftoa(
                    shift, 0, forward(shift, coarse, f, 0), &toa, &ftoa);
                KT_CHECK_EQ(toa, coarse);
                KT_CHECK_EQ(ftoa, f);
            }
        }
    }

    // A skipping loop must not skip everything.
    KT_CHECK_EQ(covered, 2u);
}

static void
test_recovery_survives_a_large_offset(void)
{
    // The stream's timestamp offset is a whole multiple of the coarse tick, so
    // it vanishes under the modulus and recovery stays exact however far into
    // an acquisition the hit falls. Checked rather than assumed, because it is
    // the property that lets this function take no offset parameter.
    const uint8_t shift = 4;

    for (uint64_t window = 0; window < 4; ++window) {
        const uint64_t coarse = (window << 14) + 1234;

        uint64_t toa = 0;
        uint8_t ftoa = 0;
        katherine_tpx3_timestamp_to_toa_ftoa(shift, 0, forward(shift, coarse, 7, 0), &toa, &ftoa);

        KT_CHECK_EQ(toa, coarse);
        KT_CHECK_EQ(ftoa, 7u);
    }
}

static void
test_recovery_undoes_a_phase_offset(void)
{
    // A phase offset is not a whole coarse tick, so leaving it in would corrupt
    // the residue the fine term comes out of. Passing it back is what keeps
    // recovery exact once phase correction is in effect.
    const uint8_t shift = 4;

    for (uint8_t phase = 0; phase < 16; ++phase) {
        const uint64_t t = forward(shift, 100, 9, phase);

        uint64_t toa = 0;
        uint8_t ftoa = 0;
        katherine_tpx3_timestamp_to_toa_ftoa(shift, phase, t, &toa, &ftoa);
        KT_CHECK_EQ(toa, 100u);
        KT_CHECK_EQ(ftoa, 9u);

        // And forgetting it is wrong for every nonzero phase, which is why the
        // parameter exists rather than being assumed zero.
        if (phase != 0) {
            katherine_tpx3_timestamp_to_toa_ftoa(shift, 0, t, &toa, &ftoa);
            KT_CHECK(toa != 100u || ftoa != 9u);
        }
    }
}

static void
test_recovery_accepts_null_outputs(void)
{
    const uint64_t t = forward(4, 55, 3, 0);

    uint64_t toa = 999;
    uint8_t ftoa = 99;

    katherine_tpx3_timestamp_to_toa_ftoa(4, 0, t, &toa, NULL);
    KT_CHECK_EQ(toa, 55u);
    KT_CHECK_EQ(ftoa, 99u);

    katherine_tpx3_timestamp_to_toa_ftoa(4, 0, t, NULL, &ftoa);
    KT_CHECK_EQ(ftoa, 3u);
}

// ------------------------------------------------------------------

static void
test_epoch_bias_is_sound_at_every_divider(void)
{
    for (uint8_t shift = 2; shift <= 5; ++shift) {
        const uint64_t fine_ticks = (uint64_t) 1 << shift;
        const uint64_t bias       = katherine_tpx3_toa_epoch_bias(shift);

        // Covers the fine field, so the decoder's subtraction cannot wrap.
        KT_CHECK(bias >= 16u);

        // A whole number of coarse ticks, so it leaves the fine term's residue
        // alone and recovery can remove it by division.
        KT_CHECK_EQ(bias % fine_ticks, 0u);

        // And no larger than it needs to be: one coarse tick once that is
        // itself big enough.
        KT_CHECK_EQ(bias, fine_ticks > 16u ? fine_ticks : 16u);
    }
}

int
main(void)
{
    KT_RUN(test_seconds_splits_exactly);
    KT_RUN(test_seconds_exact_at_the_hardware_limit);
    KT_RUN(test_seconds_accepts_null_outputs);
    KT_RUN(test_recovers_coarse_at_every_divider);
    KT_RUN(test_recovers_the_fine_term_where_it_is_meaningful);
    KT_RUN(test_recovery_survives_a_large_offset);
    KT_RUN(test_recovery_undoes_a_phase_offset);
    KT_RUN(test_recovery_accepts_null_outputs);
    KT_RUN(test_epoch_bias_is_sound_at_every_divider);
    return kt_summary();
}
