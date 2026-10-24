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

// Reaching the last chip of a three-chip allocation, because arithmetic
// about an array is not the same as indexing one.
static void
test_the_chips_are_reachable_inside_the_allocation(void)
{
    const size_t n     = 3;
    const size_t bytes = sizeof(katherine_tpx3_config_t) + n * sizeof(katherine_tpx3_chip_config_t);

    katherine_tpx3_config_t *c = (katherine_tpx3_config_t *) calloc(1, bytes);
    KT_REQUIRE(c != NULL);
    c->common.chip_count     = (uint8_t) n;
    c->chips[n - 1].polarity = KATHERINE_POLARITY_ELECTRONS;
    KT_CHECK_EQ(c->chips[n - 1].polarity, KATHERINE_POLARITY_ELECTRONS);
    KT_CHECK_EQ(c->chips[0].polarity, KATHERINE_POLARITY_HOLES);
    free(c);
}

// And that a zeroed chip really reads as the safe polarity, which the
// assertion above argues but does not perform.
static void
test_a_zeroed_chip_collects_holes(void)
{
    katherine_tpx3_chip_config_t chip;
    memset(&chip, 0, sizeof(chip));
    KT_CHECK_EQ(chip.polarity, KATHERINE_POLARITY_HOLES);
    KT_CHECK(!chip.gray_disable);
}

// Every invariant here that the compiler can settle is settled by the
// compiler. MSVC is what made the point: KT_CHECK on a constant expression
// is C4127, "conditional expression is constant", and it was right -- a
// property of the layout should fail the build rather than a test run. What
// is left for run time is the two things that genuinely execute.

// The downcast every katherine_tpx3_* entry point makes: read
// common.chip_type, refuse the configuration if it names another ASIC, and
// only then look at the rest. That is only sound while the base is first.
_Static_assert(offsetof(katherine_tpx3_config_t, common) == 0,
    "katherine_config_t must be the first member, or the chip_type check reads the wrong bytes");
_Static_assert(offsetof(katherine_config_t, chip_type) == 0,
    "chip_type must be first, so an entry point can read it before it downcasts");

// The idiom the header documents is
//
//     calloc(1, sizeof(*c) + n * sizeof(c->chips[0]))
//
// which is only enough storage while the array starts no later than the
// struct's own size. A compiler is free to place it earlier -- trailing
// padding is reusable -- and that only over-allocates, which is harmless.
_Static_assert(offsetof(katherine_tpx3_config_t, chips) <= sizeof(katherine_tpx3_config_t),
    "the documented allocation must cover the flexible array");

// Zero selects the carrier that cannot destroy the chip, which is why the
// documentation says calloc rather than malloc. Asserted rather than
// trusted, because the enumerator could be renumbered without anyone
// noticing what it was guarding.
_Static_assert((int) KATHERINE_POLARITY_HOLES == 0,
    "a zeroed configuration must select holes, which is the polarity that cannot destroy a chip");

// A budget, not a measurement. WP34 gives both structs a fixed opaque
// reserve, and these bounds are what says the reserve was sized rather than
// guessed: on this host today they are 680 and 208 bytes.
_Static_assert(sizeof(katherine_acquisition_t) <= 1024, "the acquisition has outgrown its budget");
_Static_assert(sizeof(katherine_device_t) <= 512, "the device has outgrown its budget");

// The reserves are exactly what they say. A union whose largest member is
// the byte array should be that array's size, and if padding ever made it
// otherwise, every assertion written against the macro would be measuring
// the wrong thing.
_Static_assert(sizeof(katherine_acquisition_priv_t) == KATHERINE_ACQUISITION_PRIV_SIZE,
    "the acquisition's reserve must be exactly KATHERINE_ACQUISITION_PRIV_SIZE");
_Static_assert(sizeof(katherine_device_priv_t) == KATHERINE_DEVICE_PRIV_SIZE,
    "the device's reserve must be exactly KATHERINE_DEVICE_PRIV_SIZE");
_Static_assert(alignof(katherine_acquisition_priv_t) >= alignof(uint64_t),
    "the reserve must be aligned for the widest thing the library puts in it");
_Static_assert(alignof(katherine_device_priv_t) >= alignof(uint64_t),
    "the reserve must be aligned for the widest thing the library puts in it");

int
main(void)
{
    KT_RUN(test_the_chips_are_reachable_inside_the_allocation);
    KT_RUN(test_a_zeroed_chip_collects_holes);
    return kt_summary();
}
