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

/**
 * Every code the reference implementation's map contains. Written out here
 * rather than read from the library, so that a row silently dropped from the
 * table fails instead of shrinking the expectation with it.
 */
static const uint8_t KNOWN[] = {0x01, 0x02, 0x03, 0x0A, 0x20, 0x21, 0x24, 0x25, 0x26, 0x27};

static void
test_every_known_code_is_recognized(void)
{
    for (size_t i = 0; i < sizeof(KNOWN) / sizeof(KNOWN[0]); ++i) {
        const katherine_device_derived_info_t info = katherine_device_derived_info_recognize(KNOWN[i]);

        // hw_type lives in katherine_device_info_t now, so a recognized row
        // is marked by carrying a name rather than by echoing its own key.
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
        const katherine_device_derived_info_t info = katherine_device_derived_info_recognize(unknown[i]);

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
    const katherine_device_derived_info_t gen1 = katherine_device_derived_info_recognize(0x01);
    KT_CHECK_EQ(gen1.chip_type, KATHERINE_CHIP_TPX3);
    KT_CHECK_EQ(gen1.gen, 1u);
    KT_CHECK_EQ(gen1.max_chip_count, 1u);
    KT_CHECK(gen1.supported);

    // Eight layers is what makes the static count worth carrying separately
    // from the runtime one.
    const katherine_device_derived_info_t gen2 = katherine_device_derived_info_recognize(0x03);
    KT_CHECK_EQ(gen2.chip_type, KATHERINE_CHIP_TPX3);
    KT_CHECK_EQ(gen2.gen, 2u);
    KT_CHECK_EQ(gen2.max_chip_count, 8u);
    KT_CHECK(gen2.supported);

    // The capability counts, which the guard does not read but callers do.
    // Gen2 has twice the bias supplies and twice the GPIO of Gen1, and of its
    // eight channels only four come out where a user can reach them.
    KT_CHECK_EQ(gen1.bias_supply_count, 1u);
    KT_CHECK_EQ(gen1.accessible_gpio_count, 4u);
    KT_CHECK_EQ(gen1.all_gpio_count, 4u);
    KT_CHECK_EQ(gen2.bias_supply_count, 2u);
    KT_CHECK_EQ(gen2.accessible_gpio_count, 4u);
    KT_CHECK_EQ(gen2.all_gpio_count, 8u);

    // Accessible can never exceed total, for any row.
    for (size_t i = 0; i < sizeof(KNOWN) / sizeof(KNOWN[0]); ++i) {
        const katherine_device_derived_info_t row = katherine_device_derived_info_recognize(KNOWN[i]);
        KT_CHECK(row.accessible_gpio_count <= row.all_gpio_count);
    }

    const katherine_device_derived_info_t tpx4 = katherine_device_derived_info_recognize(0x0A);
    KT_CHECK_EQ(tpx4.chip_type, KATHERINE_CHIP_TPX4);

    // HardPix is two-layer and generation 1; HardPix2 is the generation-2
    // sibling, which is the pair most easily transposed and the reason every
    // row now carries a generation: it selects the header map.
    const katherine_device_derived_info_t hardpix = katherine_device_derived_info_recognize(0x20);
    KT_CHECK_EQ(hardpix.max_chip_count, 2u);
    KT_CHECK_EQ(hardpix.gen, 1u);

    const katherine_device_derived_info_t hardpix2 = katherine_device_derived_info_recognize(0x27);
    KT_CHECK_EQ(hardpix2.gen, 2u);

    // Every recognized row states one, so nothing falls back to a guess.
    for (size_t i = 0; i < sizeof(KNOWN) / sizeof(KNOWN[0]); ++i) {
        KT_CHECK(katherine_device_derived_info_recognize(KNOWN[i]).gen >= 1u);
    }
}

// The flag now has teeth: it is what the generation-dependent calls test, so a
// row claiming it enables the acquisition-time encoding and the header map for
// that hardware. Four claim it, and all four are Timepix3 -- the Gen1 and Gen2
// Katherines, measured here, plus HardPix for Timepix3 and Monique, which this
// project has no sample of and takes on Petr's word.
//
// Pinned as a list rather than a count so that adding a row is a deliberate
// edit here as well as there. A Timepix2 or Timepix4 row turning true would
// fail this, which is the point: neither has a decoder yet.
static void
test_only_driveable_rows_claim_support(void)
{
    static const uint8_t DRIVEABLE[] = {0x01, 0x03, 0x20, 0x25};

    for (size_t i = 0; i < sizeof(KNOWN) / sizeof(KNOWN[0]); ++i) {
        const katherine_device_derived_info_t row = katherine_device_derived_info_recognize(KNOWN[i]);

        bool expected = false;
        for (size_t j = 0; j < sizeof(DRIVEABLE) / sizeof(DRIVEABLE[0]); ++j) {
            if (DRIVEABLE[j] == KNOWN[i]) expected = true;
        }

        KT_CHECK_EQ(row.supported, expected);

        // Nothing but Timepix3 has a decoder, so nothing else may claim it.
        if (row.supported) KT_CHECK_EQ(row.chip_type, KATHERINE_CHIP_TPX3);
    }

    // A zeroed structure fails the same test, which is what lets one check
    // cover both an unrecognized readout and one nobody has enumerated.
    const katherine_device_derived_info_t zeroed = {0};
    KT_CHECK(!zeroed.supported);
}

// The guard, which is what the flag is for. One test covers three states
// because they share an answer: a device nobody enumerated, a hardware type
// this version does not recognize, and one it recognizes but cannot drive all
// leave supported false, and all three must be refused rather than decoded as
// Gen1 on a guess.
static void
test_generation_dependent_calls_refuse_an_undeclared_device(void)
{
    katherine_device_t dev;
    memset(&dev, 0, sizeof(dev));

    // Nothing has enumerated or declared it. No socket is opened, which is
    // deliberate: the guard has to come before anything touches the wire, or a
    // caller learns about it from a timeout instead of an error.
    KT_CHECK_EQ(katherine_set_acq_time(&dev, 1e8), KATHERINE_E_STATE);

    // Declaring a readout this version cannot drive is refused on the same
    // test, without a second flag to keep in step.
    const katherine_device_info_t tpx2 = {.hw_type = 0x02};
    KT_CHECK_EQ(katherine_device_declare(&dev, &tpx2), KATHERINE_E_OK);
    KT_CHECK(!dev.derived_info.supported);
    KT_CHECK_EQ(katherine_set_acq_time(&dev, 1e8), KATHERINE_E_STATE);

    // A hardware type from no table row behaves the same way.
    const katherine_device_info_t unknown = {.hw_type = 0xFF};
    KT_CHECK_EQ(katherine_device_declare(&dev, &unknown), KATHERINE_E_OK);
    KT_CHECK(!dev.derived_info.supported);
    KT_CHECK_EQ(katherine_set_acq_time(&dev, 1e8), KATHERINE_E_STATE);

    KT_CHECK_EQ(katherine_device_declare(&dev, NULL), KATHERINE_E_INVAL);
}

// Declaring is the same path enumeration takes, so what it leaves behind must
// be what a probe would have left: the facts as given, and the derivations
// recognized from the hardware type.
static void
test_declaring_fills_both_records(void)
{
    katherine_device_t dev;
    memset(&dev, 0, sizeof(dev));

    const katherine_device_info_t gen2 = {
        .hw_type       = 0x03,
        .hw_revision   = 3,
        .serial_number = 13,
        .fw_version    = 5,
        .chip_count    = 1,
        .legacy        = false,
    };
    KT_CHECK_EQ(katherine_device_declare(&dev, &gen2), KATHERINE_E_OK);

    // Kept verbatim: nothing here is the library's opinion.
    KT_CHECK_EQ(dev.info.hw_type, 0x03u);
    KT_CHECK_EQ(dev.info.hw_revision, 3u);
    KT_CHECK_EQ(dev.info.serial_number, 13u);
    KT_CHECK_EQ(dev.info.fw_version, 5u);
    KT_CHECK_EQ(dev.info.chip_count, 1u);
    KT_CHECK(!dev.info.legacy);

    // Derived, and it must match what recognition gives on its own.
    const katherine_device_derived_info_t expected = katherine_device_derived_info_recognize(0x03);
    KT_CHECK_EQ(dev.derived_info.gen, expected.gen);
    KT_CHECK_EQ(dev.derived_info.max_chip_count, expected.max_chip_count);
    KT_CHECK_EQ(dev.derived_info.bias_supply_count, expected.bias_supply_count);
    KT_CHECK(dev.derived_info.supported);
    KT_CHECK_EQ(dev.derived_info.gen, 2u);

    // chip_count and max_chip_count are the pair most easily confused: one
    // says how many answered, the other how many the hardware could carry.
    KT_CHECK(dev.info.chip_count <= dev.derived_info.max_chip_count);
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

    const katherine_device_derived_info_t none = katherine_device_derived_info_recognize(0xFF);
    int n                                      = katherine_device_derived_info_snprint(buf, sizeof(buf), &none);
    KT_CHECK(n > 0);
    KT_CHECK(strcmp(buf, "device_derived_info{unrecognized}") == 0);

    const katherine_device_derived_info_t gen1 = katherine_device_derived_info_recognize(0x01);
    n                                          = katherine_device_derived_info_snprint(buf, sizeof(buf), &gen1);
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
    KT_RUN(test_only_driveable_rows_claim_support);
    KT_RUN(test_generation_dependent_calls_refuse_an_undeclared_device);
    KT_RUN(test_declaring_fills_both_records);
    KT_RUN(test_asic_names);
    KT_RUN(test_unpopulated_prints_as_such);
    return kt_summary();
}
