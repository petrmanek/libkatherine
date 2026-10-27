/**
 * \file
 * \brief A configuration whose reserved words are not zero is refused.
 *
 * Both configuration structures carry `reserved[6]`, documented "Zero.", on
 * the precedent of the kernel's copy_struct_from_user(). That precedent has
 * two halves -- leave room, and reject a non-zero tail -- and until now only
 * the first was taken: a configuration with 0xDEADBEEF in a reserved word
 * configured a readout exactly as a zeroed one did, 96 commands, no
 * complaint.
 *
 * That is the half that cannot be added later. A caller who allocates with
 * malloc() passes heap leftovers in those words, and a 2.1 that gave one of
 * them meaning would read the leftovers as a setting. Refusing now is what
 * makes the room usable.
 *
 * \author Petr Mánek
 * \date 26.10.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>

#include <katherine/katherine.h>

#include "kemu_transport.h"
#include "ktest.h"

#define CHIP_COUNT  1
#define CONFIG_SIZE (KATHERINE_TPX3_CONFIG_SIZE(CHIP_COUNT))

static katherine_device_t g_device;
static kemu_transport_t g_link;

static bool
fixture_start(void)
{
    katherine_emu_profile_t profile;
    katherine_emu_profile_defaults(&profile);
    profile.hw_type     = 0x03;
    profile.hw_revision = 3;
    profile.fw_version  = 5;

    return kemu_device(&g_device, &g_link, &profile) == KATHERINE_E_OK;
}

static void
fixture_stop(void)
{
    kemu_device_fini(&g_device, &g_link);
}

// Zeroed by calloc(), which is the allocation this contract asks callers for.
static katherine_tpx3_config_t *
make_config(void)
{
    katherine_tpx3_config_t *config = (katherine_tpx3_config_t *) calloc(1, CONFIG_SIZE);
    if (config == NULL) return NULL;

    config->common.chip_type   = KATHERINE_CHIP_TPX3;
    config->common.chip_count  = CHIP_COUNT;
    config->common.acq_time    = 400000.0;
    config->common.frame_count = 1;

    config->px_mode         = KATHERINE_TPX3_PX_TOA_TOT;
    config->fast_vco_enable = true;
    config->clock_phase     = KATHERINE_TPX3_PHASE_1;
    config->clock_frequency = KATHERINE_TPX3_FREQ_40_MHZ;

    return config;
}

// ------------------------------------------------------------------
// The control: a zeroed configuration is accepted, so the refusals below
// are about the reserved words and not about the rest of the structure.

static void
test_a_zeroed_configuration_is_accepted(void)
{
    KT_REQUIRE(fixture_start());

    katherine_tpx3_config_t *config = make_config();
    KT_REQUIRE(config != NULL);

    KT_CHECK_EQ(katherine_tpx3_configure(&g_device, config), KATHERINE_E_OK);

    free(config);
    fixture_stop();
}

// ------------------------------------------------------------------
// Every word of both blocks, one at a time: a block is only as good as its
// least-checked word, and a loop that stopped at the first is the mistake
// this guards against.

static void
test_any_dirty_reserved_word_is_refused(void)
{
    const size_t common_count = sizeof(((katherine_config_t *) 0)->reserved) / sizeof(uint32_t);
    const size_t tpx3_count   = sizeof(((katherine_tpx3_config_t *) 0)->reserved) / sizeof(uint32_t);

    for (size_t i = 0; i < common_count; ++i) {
        KT_REQUIRE(fixture_start());
        katherine_tpx3_config_t *config = make_config();
        KT_REQUIRE(config != NULL);

        config->common.reserved[i] = 0xDEADBEEFu;
        KT_CHECK_EQ(katherine_tpx3_configure(&g_device, config), KATHERINE_E_BAD_RESERVED);

        free(config);
        fixture_stop();
    }

    for (size_t i = 0; i < tpx3_count; ++i) {
        KT_REQUIRE(fixture_start());
        katherine_tpx3_config_t *config = make_config();
        KT_REQUIRE(config != NULL);

        config->reserved[i] = 1u; // the smallest non-zero value, not only the loud one
        KT_CHECK_EQ(katherine_tpx3_configure(&g_device, config), KATHERINE_E_BAD_RESERVED);

        free(config);
        fixture_stop();
    }
}

// ------------------------------------------------------------------
// The refusal happens before anything reaches the link, so a rejected
// configuration leaves the readout as it found it.

static void
test_a_refused_configuration_sends_nothing(void)
{
    KT_REQUIRE(fixture_start());

    katherine_tpx3_config_t *config = make_config();
    KT_REQUIRE(config != NULL);
    config->common.reserved[0] = 0xDEADBEEFu;

    const uint32_t before = g_link.sends;
    KT_CHECK_EQ(katherine_tpx3_configure(&g_device, config), KATHERINE_E_BAD_RESERVED);
    KT_CHECK_EQ(g_link.sends, before);

    free(config);
    fixture_stop();
}

static void
test_the_code_renders(void)
{
    KT_CHECK_STR_EQ(katherine_strerror(KATHERINE_E_BAD_RESERVED), "reserved field is not zero");
}

int
main(void)
{
    KT_RUN(test_a_zeroed_configuration_is_accepted);
    KT_RUN(test_any_dirty_reserved_word_is_refused);
    KT_RUN(test_a_refused_configuration_sends_nothing);
    KT_RUN(test_the_code_renders);
    return kt_summary();
}
