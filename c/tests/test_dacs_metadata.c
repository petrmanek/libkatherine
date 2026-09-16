/**
 * \file
 * \brief The bias DACs' ranges and their conversion to SI units.
 *
 * Two tables of the Timepix3 manual are transcribed into the library: Table
 * 11's "DAC Value" column widths, which give each DAC its maximum, and Table
 * 28's steps and quantities, which give the conversion. Both are copied
 * numbers, so both are pinned here -- a transcription slip in either is
 * invisible to every other test and to the compiler.
 *
 * Table 28 also states a range per DAC, and for six of the eighteen that
 * range disagrees with the step times the maximum. The library converts from
 * the step; this file records by how much the two differ, so the
 * disagreement is an asserted fact rather than something a later reader has
 * to rediscover and wonder about.
 *
 * \author Petr Mánek
 * \date 16.9.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <katherine/katherine.h>

#include "ktest.h"

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

/// Table 11 and Table 28, as read off the manual: the maximum, the step in SI
/// units, the quantity, and the range Table 28 states for the full scale.
// clang-format off
static const struct {
    katherine_tpx3_dac_t dac;
    const char *name;
    uint16_t max;
    double lsb;
    katherine_dac_unit_t unit;
    double stated_range;
} TABLE[] = {
    {KATHERINE_TPX3_DAC_IBIAS_PREAMP_ON,   "Ibias_Preamp_ON",   255, 20e-9,   KATHERINE_DAC_UNIT_AMP, 5.1e-6},
    {KATHERINE_TPX3_DAC_IBIAS_PREAMP_OFF,  "Ibias_Preamp_OFF",  15,  20e-9,   KATHERINE_DAC_UNIT_AMP, 300e-9},
    {KATHERINE_TPX3_DAC_VPREAMP_NCAS,      "Vpreamp_NCAS",      255, 5e-3,    KATHERINE_DAC_UNIT_VOLT,   1.275},
    {KATHERINE_TPX3_DAC_IBIAS_IKRUM,       "Ibias_Ikrum",       255, 240e-12, KATHERINE_DAC_UNIT_AMP, 60e-9},
    {KATHERINE_TPX3_DAC_VFBK,              "Vfbk",              255, 5e-3,    KATHERINE_DAC_UNIT_VOLT,   1.275},
    {KATHERINE_TPX3_DAC_VTHRESHOLD_FINE,   "Vthreshold_fine",   511, 500e-6,  KATHERINE_DAC_UNIT_VOLT,   255e-3},
    {KATHERINE_TPX3_DAC_VTHRESHOLD_COARSE, "Vthreshold_coarse", 15,  80e-3,   KATHERINE_DAC_UNIT_VOLT,   1.19},
    {KATHERINE_TPX3_DAC_IBIAS_DISCS1_ON,   "Ibias_DiscS1_ON",   255, 20e-9,   KATHERINE_DAC_UNIT_AMP, 5.1e-6},
    {KATHERINE_TPX3_DAC_IBIAS_DISCS1_OFF,  "Ibias_DiscS1_OFF",  15,  20e-9,   KATHERINE_DAC_UNIT_AMP, 300e-9},
    {KATHERINE_TPX3_DAC_IBIAS_DISCS2_ON,   "Ibias_DiscS2_ON",   255, 13e-9,   KATHERINE_DAC_UNIT_AMP, 3.4e-6},
    {KATHERINE_TPX3_DAC_IBIAS_DISCS2_OFF,  "Ibias_DiscS2_OFF",  15,  13e-9,   KATHERINE_DAC_UNIT_AMP, 200e-9},
    {KATHERINE_TPX3_DAC_IBIAS_PIXELDAC,    "Ibias_PixelDAC",    255, 1.08e-9, KATHERINE_DAC_UNIT_AMP, 273e-9},
    {KATHERINE_TPX3_DAC_IBIAS_TPBUFFERIN,  "Ibias_TPbufferIn",  255, 40e-9,   KATHERINE_DAC_UNIT_AMP, 10.2e-6},
    {KATHERINE_TPX3_DAC_IBIAS_TPBUFFEROUT, "Ibias_TPbufferOut", 255, 1e-6,    KATHERINE_DAC_UNIT_AMP, 255e-6},
    {KATHERINE_TPX3_DAC_VTP_COARSE,        "VTP_coarse",        255, 5e-3,    KATHERINE_DAC_UNIT_VOLT,   1.275},
    {KATHERINE_TPX3_DAC_VTP_FINE,          "VTP_fine",          511, 2.5e-3,  KATHERINE_DAC_UNIT_VOLT,   1.275},
    {KATHERINE_TPX3_DAC_IBIAS_CP_PLL,      "Ibias_CP_PLL",      255, 600e-9,  KATHERINE_DAC_UNIT_AMP, 153e-6},
    {KATHERINE_TPX3_DAC_PLL_VCNTRL,        "PLL_Vcntrl",        255, 5.7e-3,  KATHERINE_DAC_UNIT_VOLT,   1.35},
};
// clang-format on

// ------------------------------------------------------------------
// 1. The ranges, and the enumeration's order.

/// This file's copy of the manual must cover every DAC the library declares.
/// A compile-time invariant, so asserted at compile time -- and MSVC /W4
/// objects (C4127) to a runtime test of a constant, rightly.
_Static_assert(COUNT(TABLE) == KATHERINE_TPX3_DAC_COUNT, "TABLE must have one row per DAC");

static void
test_maxima(void)
{
    for (size_t i = 0; i < COUNT(TABLE); ++i) {
        // The enumerator's value is its index, which is what makes it usable
        // against katherine_tpx3_dacs_t::array and what the library's own table
        // relies on.
        KT_CHECK_EQ((unsigned) TABLE[i].dac, i);
        KT_CHECK_EQ(katherine_tpx3_dac_max(TABLE[i].dac), TABLE[i].max);
    }

    // Outside the enumeration, in both directions.
    KT_CHECK_EQ(katherine_tpx3_dac_max((katherine_tpx3_dac_t) KATHERINE_TPX3_DAC_COUNT), 0);
    KT_CHECK_EQ(katherine_tpx3_dac_max((katherine_tpx3_dac_t) -1), 0);
}

/// The maxima are what katherine_tpx3_dacs_validate() accepts, so the two must
/// agree -- and this is what makes the maximum inclusive rather than a
/// bound: a vector at every maximum passes, and one over any of them fails.
static void
test_maxima_agree_with_validate(void)
{
    katherine_tpx3_dacs_t dacs;

    memset(&dacs, 0, sizeof(dacs));
    for (size_t i = 0; i < COUNT(TABLE); ++i) {
        dacs.array[i] = katherine_tpx3_dac_max(TABLE[i].dac);
    }
    KT_CHECK_EQ(katherine_tpx3_dacs_validate(&dacs), KATHERINE_E_OK);

    for (size_t i = 0; i < COUNT(TABLE); ++i) {
        ++dacs.array[i];
        KT_CHECK_EQ(katherine_tpx3_dacs_validate(&dacs), KATHERINE_E_INVAL);
        --dacs.array[i];
    }
}

// ------------------------------------------------------------------
// 2. The conversion.

static void
test_conversion(void)
{
    for (size_t i = 0; i < COUNT(TABLE); ++i) {
        katherine_dac_unit_t unit = (katherine_dac_unit_t) 99;

        // Zero converts to zero at every DAC: the scale has no offset.
        KT_CHECK_EQ(katherine_tpx3_dac_to_si(TABLE[i].dac, 0, &unit), 0.0);
        KT_CHECK_EQ((int) unit, (int) TABLE[i].unit);

        // One LSB, and the full scale, both from the step.
        KT_CHECK_CLOSE(katherine_tpx3_dac_to_si(TABLE[i].dac, 1, NULL), TABLE[i].lsb);
        KT_CHECK_CLOSE(katherine_tpx3_dac_to_si(TABLE[i].dac, TABLE[i].max, NULL),
            TABLE[i].lsb * (double) TABLE[i].max);

        // Linear throughout, not merely at the ends.
        KT_CHECK_CLOSE(katherine_tpx3_dac_to_si(TABLE[i].dac, 7, NULL), 7.0 * TABLE[i].lsb);
    }

    // A NULL unit is accepted, and an out-of-range DAC converts to zero
    // without touching the caller's unit.
    katherine_dac_unit_t unit = KATHERINE_DAC_UNIT_VOLT;
    KT_CHECK_EQ(katherine_tpx3_dac_to_si((katherine_tpx3_dac_t) KATHERINE_TPX3_DAC_COUNT, 100, &unit), 0.0);
    KT_CHECK_EQ((int) unit, (int) KATHERINE_DAC_UNIT_VOLT);
}

/// Which DACs bias a current and which set a voltage. Asserted as a count as
/// well as per DAC, so that a unit flipped in the library's table shows up
/// even if this file's copy were flipped with it.
static void
test_units(void)
{
    size_t amperes = 0, volts = 0;

    for (size_t i = 0; i < COUNT(TABLE); ++i) {
        katherine_dac_unit_t unit;
        (void) katherine_tpx3_dac_to_si(TABLE[i].dac, 1, &unit);

        KT_CHECK_EQ((int) unit, (int) TABLE[i].unit);
        if (unit == KATHERINE_DAC_UNIT_AMP) {
            ++amperes;
        } else {
            ++volts;
        }

        // The name says which, every one of the eighteen: Ibias_* are
        // currents and V* are voltages, with no exceptions in Table 28.
        const bool named_current = strncmp(TABLE[i].name, "Ibias_", 6) == 0;
        KT_CHECK_EQ(named_current, unit == KATHERINE_DAC_UNIT_AMP);
    }

    KT_CHECK_EQ(amperes, 11);
    KT_CHECK_EQ(volts, 7);
}

// ------------------------------------------------------------------
// 3. Table 28 against itself.

/// The step times the maximum, against the range Table 28 also states.
///
/// Twelve rows agree to a fraction of a percent. Six do not, and the
/// deviations are written out here so the disagreement cannot be mistaken
/// later for a transcription error in this library. The library converts from
/// the step, which the manual corroborates where it can -- see
/// test_threshold_corroborates_the_coarse_step() below.
static void
test_stated_ranges(void)
{
    // Per DAC, the tolerated deviation of lsb * max from the stated range.
    // Anything not listed must land within 0.5%.
    static const struct {
        katherine_tpx3_dac_t dac;
        double deviation;
    } DISCREPANT[] = {
        {KATHERINE_TPX3_DAC_IBIAS_IKRUM, +0.020},
        {KATHERINE_TPX3_DAC_VTHRESHOLD_COARSE, +0.008},
        {KATHERINE_TPX3_DAC_IBIAS_DISCS2_ON, -0.025},
        {KATHERINE_TPX3_DAC_IBIAS_DISCS2_OFF, -0.025},
        {KATHERINE_TPX3_DAC_IBIAS_PIXELDAC, +0.009},
        {KATHERINE_TPX3_DAC_PLL_VCNTRL, +0.077},
    };

    for (size_t i = 0; i < COUNT(TABLE); ++i) {
        const double full      = katherine_tpx3_dac_to_si(TABLE[i].dac, TABLE[i].max, NULL);
        const double deviation = (full - TABLE[i].stated_range) / TABLE[i].stated_range;

        double expected = 0.0;
        for (size_t j = 0; j < COUNT(DISCREPANT); ++j) {
            if (DISCREPANT[j].dac == TABLE[i].dac) {
                expected = DISCREPANT[j].deviation;
                break;
            }
        }

        // 0.002 absolute on a fraction: tight enough to catch a changed
        // number, loose enough for the manual's own rounding.
        KT_CHECK_NEAR(deviation, expected, 0.002);
    }
}

/// The manual's combined threshold row corroborates the coarse step.
///
/// Table 28 gives Vthreshold, the 13-bit concatenation of coarse and fine, a
/// range of 0 to 1.45 V. Summing the two parts' own full scales reproduces
/// that, which says the 80 mV coarse step is right and the 1.19 V coarse
/// range is the rounded figure -- the one place the manual checks itself, and
/// the reason this library scales by the step.
static void
test_threshold_corroborates_the_coarse_step(void)
{
    const double coarse = katherine_tpx3_dac_to_si(KATHERINE_TPX3_DAC_VTHRESHOLD_COARSE,
        katherine_tpx3_dac_max(KATHERINE_TPX3_DAC_VTHRESHOLD_COARSE), NULL);
    const double fine   = katherine_tpx3_dac_to_si(KATHERINE_TPX3_DAC_VTHRESHOLD_FINE,
        katherine_tpx3_dac_max(KATHERINE_TPX3_DAC_VTHRESHOLD_FINE), NULL);

    // 5 mV of the manual's own 1.45 V, which it states to three digits.
    KT_CHECK_NEAR(coarse + fine, 1.45, 0.006);
}

int
main(void)
{
    KT_RUN(test_maxima);
    KT_RUN(test_maxima_agree_with_validate);
    KT_RUN(test_conversion);
    KT_RUN(test_units);
    KT_RUN(test_stated_ranges);
    KT_RUN(test_threshold_corroborates_the_coarse_step);
    return kt_summary();
}
