/**
 * @file
 * @brief Range validation of DAC register values against Tpx3 manual Table 11.
 *
 * katherine_dacs_validate() is opt-in: katherine_set_dacs() (config.c) sends
 * every register unchecked and lets the chip truncate an out-of-range value
 * silently instead of rejecting it (Tpx3 manual Table 11: each of the 18
 * DACs is 4, 8 or 9 bits wide). This freezes the per-DAC maxima the
 * validator checks against, in katherine_dacs_named_t / array order (chip
 * DAC Code 1..18):
 *
 *   0  Ibias_Preamp_ON     255 (8 bit)   9  Ibias_DiscS2_ON    255 (8 bit)
 *   1  Ibias_Preamp_OFF     15 (4 bit)  10  Ibias_DiscS2_OFF    15 (4 bit)
 *   2  VPReamp_NCAS        255 (8 bit)  11  Ibias_PixelDAC     255 (8 bit)
 *   3  Ibias_Ikrum         255 (8 bit)  12  Ibias_TPbufferIn   255 (8 bit)
 *   4  Vfbk                255 (8 bit)  13  Ibias_TPbufferOut  255 (8 bit)
 *   5  Vthreshold_fine     511 (9 bit)  14  VTP_coarse         255 (8 bit)
 *   6  Vthreshold_coarse    15 (4 bit)  15  VTP_fine           511 (9 bit)
 *   7  Ibias_DiscS1_ON     255 (8 bit)  16  Ibias_CP_PLL       255 (8 bit)
 *   8  Ibias_DiscS1_OFF     15 (4 bit)  17  PLL_Vcntrl         255 (8 bit)
 *
 * Vectors: every DAC at its own maximum passes; each single DAC one past its
 * maximum (every other DAC still at its own maximum) fails.
 *
 * A standalone file rather than an addition to test_cmd_encoders.c: that
 * file freezes wire-format byte vectors of the command encoders, a
 * different concern from validating input ranges before any command is
 * built. Plain, portable C with no sockets, so it builds and runs
 * everywhere and claims no resource, like test_bitfields.c.
 *
 * @author Petr Mánek
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>

#include <katherine/config.h>
#include <katherine/error.h>

#include "ktest.h"

/* Table 11 maxima, in katherine_dacs_named_t / array index order -- see the
   file header for the DAC each index names. */
static const uint16_t DAC_MAX[18] = {
    255,
    15,
    255,
    255,
    255,
    511,
    15,
    255,
    15,
    255,
    15,
    255,
    255,
    255,
    255,
    511,
    255,
    255,
};

static void
fill_at_max(katherine_dacs_t *dacs)
{
    for (int i = 0; i < 18; ++i) {
        dacs->array[i] = DAC_MAX[i];
    }
}

static void
test_all_max_passes(void)
{
    katherine_dacs_t dacs;
    fill_at_max(&dacs);
    KT_CHECK_EQ(katherine_dacs_validate(&dacs), 0);
}

static void
test_each_dac_max_plus_one_fails(void)
{
    for (int i = 0; i < 18; ++i) {
        katherine_dacs_t dacs;
        fill_at_max(&dacs);
        dacs.array[i] = (uint16_t) (DAC_MAX[i] + 1);
        KT_CHECK_EQ(katherine_dacs_validate(&dacs), -KATHERINE_E_INVAL);
    }
}

int
main(void)
{
    KT_RUN(test_all_max_passes);
    KT_RUN(test_each_dac_max_plus_one_fails);

    return kt_summary();
}
