/**
 * @file
 * @brief Pixel-clock phase counts and where fast time stamping is coherent.
 *
 * The whole of Timepix3 manual Table 17 (p39) for DualEdgeClock = 1, which is
 * the value katherine_pll_config_word() pins, pinned here cell by cell. The
 * table matters because katherine_phase_t names what is requested rather than
 * what the clock divider grants: PHASE_16 yields sixteen phases at FREQ_40 and
 * four at FREQ_160, so anything dividing a coarse tick by the phase count
 * cannot read that count off the enumerator.
 *
 * Vectors transcribed from the manual page, not from the library, and not from
 * the summary in the audit notes -- that summary omitted the FREQ_20 row
 * altogether, which is exactly the sort of gap a table copied from prose
 * inherits.
 *
 * @author Petr Mánek
 * @date 27.8.26
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
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
    /* Rows in katherine_freq_t order, columns in katherine_phase_t order. */
    static const uint8_t expected[4][5] = {
        /*              PHASE_1 PHASE_2 PHASE_4 PHASE_8 PHASE_16 */
        /* FREQ_20  */ {1, 2, 4, 8, 16},
        /* FREQ_40  */ {1, 2, 4, 8, 16},
        /* FREQ_80  */ {1, 1, 2, 4, 8},
        /* FREQ_160 */ {1, 1, 1, 2, 4},
    };

    for (int f = FREQ_20; f <= FREQ_160; ++f) {
        for (int p = PHASE_1; p <= PHASE_16; ++p) {
            KT_CHECK_EQ(katherine_actual_phases((katherine_freq_t) f, (katherine_phase_t) p), expected[f][p]);
        }
    }
}

static void
test_actual_phases_clamping_is_real(void)
{
    /* The three cells the naming would get wrong, called out on their own so a
       regression names the case rather than an index. */
    KT_CHECK_EQ(katherine_actual_phases(FREQ_160, PHASE_16), 4);
    KT_CHECK_EQ(katherine_actual_phases(FREQ_80, PHASE_16), 8);
    KT_CHECK_EQ(katherine_actual_phases(FREQ_160, PHASE_2), 1);

    /* And the one where the name does hold, so the test is not merely
       asserting that everything is clamped. */
    KT_CHECK_EQ(katherine_actual_phases(FREQ_40, PHASE_16), 16);
}

static void
test_actual_phases_rejects_out_of_range(void)
{
    /* 0 means "no answer", which is distinguishable from every real count:
       the table's smallest entry is 1. */
    KT_CHECK_EQ(katherine_actual_phases((katherine_freq_t) (FREQ_160 + 1), PHASE_1), 0);
    KT_CHECK_EQ(katherine_actual_phases(FREQ_40, (katherine_phase_t) (PHASE_16 + 1)), 0);
    KT_CHECK_EQ(katherine_actual_phases((katherine_freq_t) -1, PHASE_1), 0);
    KT_CHECK_EQ(katherine_actual_phases(FREQ_40, (katherine_phase_t) -1), 0);
}

static void
test_fast_vco_supported_only_at_40(void)
{
    KT_CHECK(!katherine_freq_is_fast_vco_supported(FREQ_20));
    KT_CHECK(katherine_freq_is_fast_vco_supported(FREQ_40));
    KT_CHECK(!katherine_freq_is_fast_vco_supported(FREQ_80));
    KT_CHECK(!katherine_freq_is_fast_vco_supported(FREQ_160));
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
