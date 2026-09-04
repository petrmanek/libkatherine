/**
 * \file
 * \brief Recognizing a readout from the hardware type it reports.
 *
 * The recognition map is the one part of the capability table with an upstream
 * source. Everything asserted here is asserted about that map: which codes are
 * known, what each says, and that an unknown code produces a structure a caller
 * can tell apart from a populated one.
 *
 * Nothing here claims a readout behaves in a particular way. The table
 * deliberately carries no behavioural flags, because only one of its ten rows
 * can be driven and tested, and a flag that cannot be checked is a guess with a
 * struct field around it.
 *
 * \author Petr Mánek
 * \date 31.8.26
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

#include "ktest.h"

/// Every code the reference implementation's map contains. Written out here
/// rather than read from the library, so that a row silently dropped from the
/// table fails instead of shrinking the expectation with it.
static const uint8_t KNOWN[] = {0x01, 0x02, 0x03, 0x0A, 0x20, 0x21, 0x24, 0x25, 0x26, 0x27};

static void
test_every_known_code_is_recognized(void)
{
    for (size_t i = 0; i < sizeof(KNOWN) / sizeof(KNOWN[0]); ++i) {
        const katherine_device_info_t info = katherine_device_info_recognize(KNOWN[i]);

        KT_CHECK_EQ(info.hw_type, KNOWN[i]);
        KT_CHECK(info.name != NULL);
        KT_CHECK(info.chip_type != KATHERINE_CHIP_UNKNOWN);

        // Every readout carries at least one sensor, whatever else is unknown
        // about it.
        KT_CHECK(info.max_chip_count >= 1);
    }
}

static void
test_unknown_codes_report_themselves_as_unpopulated(void)
{
    // 0 is the sentinel itself; the rest are codes no readout reports.
    const uint8_t unknown[] = {0x00, 0x04, 0x11, 0x22, 0x28, 0x7F, 0xFF};

    for (size_t i = 0; i < sizeof(unknown) / sizeof(unknown[0]); ++i) {
        const katherine_device_info_t info = katherine_device_info_recognize(unknown[i]);

        KT_CHECK_EQ(info.hw_type, 0u);
        KT_CHECK(info.name == NULL);
        KT_CHECK_EQ(info.chip_type, KATHERINE_CHIP_UNKNOWN);
        KT_CHECK_EQ(info.max_chip_count, 0u);
        KT_CHECK(!info.supported);
    }
}

// The specific claims, spelled out. A table is easy to reorder or mistype and
// the compiler cannot tell; these are the rows a reader would want to trust.
static void
test_the_readouts_we_can_name(void)
{
    const katherine_device_info_t gen1 = katherine_device_info_recognize(0x01);
    KT_CHECK_EQ(gen1.chip_type, KATHERINE_CHIP_TPX3);
    KT_CHECK_EQ(gen1.gen, 1u);
    KT_CHECK_EQ(gen1.max_chip_count, 1u);
    KT_CHECK(gen1.supported);

    // Eight layers is what makes the static count worth carrying separately
    // from the runtime one.
    const katherine_device_info_t gen2 = katherine_device_info_recognize(0x03);
    KT_CHECK_EQ(gen2.chip_type, KATHERINE_CHIP_TPX3);
    KT_CHECK_EQ(gen2.gen, 2u);
    KT_CHECK_EQ(gen2.max_chip_count, 8u);
    KT_CHECK(!gen2.supported);

    const katherine_device_info_t tpx4 = katherine_device_info_recognize(0x0A);
    KT_CHECK_EQ(tpx4.chip_type, KATHERINE_CHIP_TPX4);

    // HardPix is two-layer, and its generation is not stated by the source, so
    // the table says 0 rather than inventing one.
    const katherine_device_info_t hardpix = katherine_device_info_recognize(0x20);
    KT_CHECK_EQ(hardpix.max_chip_count, 2u);
    KT_CHECK_EQ(hardpix.gen, 0u);
}

// Exactly one row may claim to be driveable, and it must be the one we test
// against. If a second ever turns true without hardware behind it, this fails.
static void
test_only_one_row_claims_support(void)
{
    unsigned supported = 0;

    for (size_t i = 0; i < sizeof(KNOWN) / sizeof(KNOWN[0]); ++i) {
        if (katherine_device_info_recognize(KNOWN[i]).supported) ++supported;
    }

    KT_CHECK_EQ(supported, 1u);
    KT_CHECK(katherine_device_info_recognize(0x01).supported);
}

static void
test_asic_names(void)
{
    KT_CHECK(strcmp(katherine_str_chip_type(KATHERINE_CHIP_TPX2), "Timepix2") == 0);
    KT_CHECK(strcmp(katherine_str_chip_type(KATHERINE_CHIP_TPX3), "Timepix3") == 0);
    KT_CHECK(strcmp(katherine_str_chip_type(KATHERINE_CHIP_TPX4), "Timepix4") == 0);
    KT_CHECK(strcmp(katherine_str_chip_type(KATHERINE_CHIP_UNKNOWN), "(unknown)") == 0);

    // A value outside the enumeration must not walk off the switch.
    KT_CHECK(strcmp(katherine_str_chip_type((katherine_chip_type_t) 99), "(unknown)") == 0);
}

// An unpopulated structure has to be distinguishable in output too, or a log
// line reads as a readout that answered with blanks.
static void
test_unpopulated_prints_as_such(void)
{
    char buf[128];

    const katherine_device_info_t none = katherine_device_info_recognize(0xFF);
    int n                              = katherine_device_info_snprint(buf, sizeof(buf), &none);
    KT_CHECK(n > 0);
    KT_CHECK(strcmp(buf, "device_info{not populated}") == 0);

    const katherine_device_info_t gen1 = katherine_device_info_recognize(0x01);
    n                                  = katherine_device_info_snprint(buf, sizeof(buf), &gen1);
    KT_CHECK(n > 0);
    KT_CHECK(strstr(buf, "Katherine for Timepix3") != NULL);
    KT_CHECK(strstr(buf, "Timepix3") != NULL);
}

int
main(void)
{
    KT_RUN(test_every_known_code_is_recognized);
    KT_RUN(test_unknown_codes_report_themselves_as_unpopulated);
    KT_RUN(test_the_readouts_we_can_name);
    KT_RUN(test_only_one_row_claims_support);
    KT_RUN(test_asic_names);
    KT_RUN(test_unpopulated_prints_as_such);
    return kt_summary();
}
