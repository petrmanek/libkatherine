/**
 * \file
 * \brief Pixel-clock phase counts and where fast time stamping is coherent.
 *
 * The whole of Timepix3 manual Table 17 (p39) for DualEdgeClock = 1, which is
 * the value katherine_pll_config_word() pins, pinned here cell by cell. The
 * table matters because katherine_tpx3_phase_t names what is requested rather
 * than what the clock divider grants: KATHERINE_TPX3_PHASE_16 yields sixteen
 * phases at KATHERINE_TPX3_FREQ_40_MHZ and four at
 * KATHERINE_TPX3_FREQ_160_MHZ, so anything dividing a coarse tick by the
 * phase count cannot read that count off the enumerator.
 *
 * Vectors transcribed from the manual page, not from the library, and not
 * from the summary in the audit notes -- that summary omitted the
 * KATHERINE_TPX3_FREQ_20_MHZ row altogether, which is exactly the sort of gap
 * a table copied from prose inherits.
 *
 * \author Petr Mánek
 * \date 27.8.26
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

static void
test_actual_phases_table(void)
{
    // clang-format off
    // Rows follow katherine_tpx3_freq_t, columns katherine_tpx3_phase_t, and
    // both axes are labelled by number alone -- as in the table under test.
    //
    // MHz \ phases  1  2  4  8  16
    static const uint8_t expected[4][5] = {
        /*  20 */   {  1, 2, 4, 8, 16 },
        /*  40 */   {  1, 2, 4, 8, 16 },
        /*  80 */   {  1, 1, 2, 4,  8 },
        /* 160 */   {  1, 1, 1, 2,  4 },
    };
    // clang-format on

    for (int f = KATHERINE_TPX3_FREQ_20_MHZ; f <= KATHERINE_TPX3_FREQ_160_MHZ; ++f) {
        for (int p = KATHERINE_TPX3_PHASE_1; p <= KATHERINE_TPX3_PHASE_16; ++p) {
            KT_CHECK_EQ(katherine_actual_phases((katherine_tpx3_freq_t) f, (katherine_tpx3_phase_t) p), expected[f][p]);
        }
    }
}

static void
test_actual_phases_clamping_is_real(void)
{
    // The three cells the naming would get wrong, called out on their own so a
    // regression names the case rather than an index.
    KT_CHECK_EQ(katherine_actual_phases(KATHERINE_TPX3_FREQ_160_MHZ, KATHERINE_TPX3_PHASE_16), 4);
    KT_CHECK_EQ(katherine_actual_phases(KATHERINE_TPX3_FREQ_80_MHZ, KATHERINE_TPX3_PHASE_16), 8);
    KT_CHECK_EQ(katherine_actual_phases(KATHERINE_TPX3_FREQ_160_MHZ, KATHERINE_TPX3_PHASE_2), 1);

    // And the one where the name does hold, so the test is not merely
    // asserting that everything is clamped.
    KT_CHECK_EQ(katherine_actual_phases(KATHERINE_TPX3_FREQ_40_MHZ, KATHERINE_TPX3_PHASE_16), 16);
}

static void
test_actual_phases_rejects_out_of_range(void)
{
    // 0 means "no answer", which is distinguishable from every real count:
    // the table's smallest entry is 1.
    KT_CHECK_EQ(katherine_actual_phases((katherine_tpx3_freq_t) (KATHERINE_TPX3_FREQ_160_MHZ + 1), KATHERINE_TPX3_PHASE_1), 0);
    KT_CHECK_EQ(katherine_actual_phases(KATHERINE_TPX3_FREQ_40_MHZ, (katherine_tpx3_phase_t) (KATHERINE_TPX3_PHASE_16 + 1)), 0);
    KT_CHECK_EQ(katherine_actual_phases((katherine_tpx3_freq_t) -1, KATHERINE_TPX3_PHASE_1), 0);
    KT_CHECK_EQ(katherine_actual_phases(KATHERINE_TPX3_FREQ_40_MHZ, (katherine_tpx3_phase_t) -1), 0);
}

static void
test_fast_vco_supported_only_at_40(void)
{
    KT_CHECK(!katherine_freq_is_fast_vco_supported(KATHERINE_TPX3_FREQ_20_MHZ));
    KT_CHECK(katherine_freq_is_fast_vco_supported(KATHERINE_TPX3_FREQ_40_MHZ));
    KT_CHECK(!katherine_freq_is_fast_vco_supported(KATHERINE_TPX3_FREQ_80_MHZ));
    KT_CHECK(!katherine_freq_is_fast_vco_supported(KATHERINE_TPX3_FREQ_160_MHZ));
}

int
main(void)
{
    KT_RUN(test_actual_phases_table);
    KT_RUN(test_actual_phases_clamping_is_real);
    KT_RUN(test_actual_phases_rejects_out_of_range);
    KT_RUN(test_fast_vco_supported_only_at_40);
    return kt_summary();
}
