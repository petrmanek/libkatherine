/**
 * \file
 * \brief The structural invariants the configuration restructure rests on.
 *
 * Not a freeze of every byte: sizes move with the compiler and the word
 * size, and pinning them would fail on MSVC for reasons that mean nothing.
 * What is pinned here is what the library and its callers actually rely on
 * -- where a struct begins, where its flexible array begins, and that the
 * allocation idiom the documentation gives covers it -- plus a budget on
 * the two structs WP34 is about to grow, so that growth is deliberate.
 *
 * \author Petr Mánek
 * \date 23.10.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <katherine/katherine.h>

#include "ktest.h"

// The downcast every katherine_tpx3_* entry point makes: read
// common.chip_type, refuse the configuration if it names another ASIC, and
// only then look at the rest. That is only sound while the base is first.
static void
test_base_is_first(void)
{
    KT_CHECK_EQ(offsetof(katherine_tpx3_config_t, common), 0u);
    KT_CHECK_EQ(offsetof(katherine_config_t, chip_type), 0u);
}

// The idiom the header documents is
//
//     calloc(1, sizeof(*c) + n * sizeof(c->chips[0]))
//
// which is only enough storage while the array starts no later than the
// struct's own size. A compiler is free to place it earlier -- trailing
// padding is reusable -- and that only over-allocates, which is harmless.
static void
test_allocation_idiom_covers_the_array(void)
{
    KT_CHECK(offsetof(katherine_tpx3_config_t, chips) <= sizeof(katherine_tpx3_config_t));

    const size_t n     = 3;
    const size_t bytes = sizeof(katherine_tpx3_config_t) + n * sizeof(katherine_tpx3_chip_config_t);
    KT_CHECK(bytes >= offsetof(katherine_tpx3_config_t, chips) + n * sizeof(katherine_tpx3_chip_config_t));

    // And the chips really are reachable within it, which the arithmetic
    // above asserts but does not exercise.
    katherine_tpx3_config_t *c = (katherine_tpx3_config_t *) calloc(1, bytes);
    KT_REQUIRE(c != NULL);
    c->common.chip_count     = (uint8_t) n;
    c->chips[n - 1].polarity = KATHERINE_POLARITY_ELECTRONS;
    KT_CHECK_EQ(c->chips[n - 1].polarity, KATHERINE_POLARITY_ELECTRONS);
    KT_CHECK_EQ(c->chips[0].polarity, KATHERINE_POLARITY_HOLES);
    free(c);
}

// Zero selects the carrier that cannot destroy the chip, which is why the
// documentation says calloc rather than malloc. Asserted rather than
// trusted, because the enumerator could be renumbered without anyone
// noticing what it was guarding.
static void
test_zero_is_the_safe_polarity(void)
{
    KT_CHECK_EQ((int) KATHERINE_POLARITY_HOLES, 0);

    katherine_tpx3_chip_config_t chip;
    memset(&chip, 0, sizeof(chip));
    KT_CHECK_EQ(chip.polarity, KATHERINE_POLARITY_HOLES);
    KT_CHECK(!chip.gray_disable);
}

// A budget, not a measurement. WP34 gives both structs a fixed opaque
// reserve, and these bounds are what says the reserve was sized rather than
// guessed: on this host today they are 560 and 152 bytes.
static void
test_public_structs_stay_within_budget(void)
{
    KT_CHECK(sizeof(katherine_acquisition_t) <= 1024);
    KT_CHECK(sizeof(katherine_device_t) <= 512);

    // The reserve is exactly what it says. A union whose largest member is
    // the byte array should be that array's size, and if padding ever made
    // it otherwise, every assertion sized against the macro would be
    // measuring the wrong thing.
    KT_CHECK_EQ(sizeof(katherine_acquisition_priv_t), (size_t) KATHERINE_ACQUISITION_PRIV_SIZE);
    KT_CHECK(alignof(katherine_acquisition_priv_t) >= alignof(uint64_t));

    // Both hold uint64_t members, so neither may be aligned more loosely.
    KT_CHECK(alignof(katherine_acquisition_t) >= alignof(uint64_t));
    KT_CHECK(alignof(katherine_device_t) >= alignof(uint64_t));
}

int
main(void)
{
    KT_RUN(test_base_is_first);
    KT_RUN(test_allocation_idiom_covers_the_array);
    KT_RUN(test_zero_is_the_safe_polarity);
    KT_RUN(test_public_structs_stay_within_budget);
    return kt_summary();
}
