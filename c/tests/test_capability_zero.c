/**
 * \file
 * \brief A capability count of zero means the readout has none of that hardware.
 *
 * The capability table carries four rows whose GPIO, timing and pulse
 * counts are all zero, and two of them -- HardPix for Timepix3 (0x20) and
 * Monique (0x25) -- are marked supported. On those, before this:
 *
 *   - katherine_set_gpio_config() wrote three registers and reported
 *     success, on hardware that cannot have obeyed;
 *   - katherine_get_tdc_counts() sent its command and read none of the
 *     answers, which the comment above that very loop says poisons the
 *     next command.
 *
 * A15 settled the meaning: zero is "none", not "unknown". So both refuse
 * with KATHERINE_E_UNSUPPORTED, and the pulse generator gates on its own
 * count rather than on the generation -- equivalent on every row of today's
 * table, and still right if a later row pairs one with the other.
 *
 * The emulator will claim any hw_type, so these run without the hardware
 * nobody here has.
 *
 * \author Petr Mánek
 * \date 26.10.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <katherine/katherine.h>

#include "kemu_transport.h"
#include "ktest.h"

#define HW_HARDPIX 0x20 ///< Supported, and every capability count zero.
#define HW_GEN2    0x03 ///< Supported, and every capability count non-zero.

static katherine_device_t g_device;
static kemu_transport_t g_link;

static bool
fixture_start(uint8_t hw_type)
{
    katherine_emu_profile_t profile;
    katherine_emu_profile_defaults(&profile);
    profile.hw_type     = hw_type;
    profile.hw_revision = 3;
    profile.fw_version  = 5;

    return kemu_device(&g_device, &g_link, &profile) == KATHERINE_E_OK;
}

static void
fixture_stop(void)
{
    kemu_device_fini(&g_device, &g_link);
}

// ------------------------------------------------------------------
// The row under test really is the one with zero counts, so a refusal
// below is about the count and not about the readout being unknown.

static void
test_the_zero_row_is_supported_and_empty(void)
{
    KT_REQUIRE(fixture_start(HW_HARDPIX));

    KT_CHECK(g_device.derived_info.supported);
    KT_CHECK_EQ(g_device.derived_info.all_gpio_count, 0);
    KT_CHECK_EQ(g_device.derived_info.tdc_channel_count, 0);
    KT_CHECK_EQ(g_device.derived_info.pulse_channel_count, 0);

    fixture_stop();
}

static void
test_a_row_with_no_gpio_refuses_gpio(void)
{
    KT_REQUIRE(fixture_start(HW_HARDPIX));

    const katherine_gpio_config_t config = {0};
    const uint32_t before                = g_link.sends;

    KT_CHECK_EQ(katherine_set_gpio_config(&g_device, &config), KATHERINE_E_UNSUPPORTED);
    KT_CHECK_EQ(g_link.sends, before); // and nothing went out

    fixture_stop();
}

static void
test_a_row_with_no_timing_channels_refuses_counts(void)
{
    KT_REQUIRE(fixture_start(HW_HARDPIX));

    katherine_tdc_counts_t counts;
    const uint32_t before = g_link.sends;

    KT_CHECK_EQ(katherine_get_tdc_counts(&g_device, &counts), KATHERINE_E_UNSUPPORTED);
    KT_CHECK_EQ(g_link.sends, before);

    fixture_stop();
}

static void
test_a_row_with_no_pulse_channels_refuses_the_generator(void)
{
    KT_REQUIRE(fixture_start(HW_HARDPIX));

    const katherine_pulse_config_t config = {0};
    katherine_pulse_counts_t counts;

    KT_CHECK_EQ(katherine_set_pulse_generator(&g_device, &config), KATHERINE_E_UNSUPPORTED);
    KT_CHECK_EQ(katherine_get_pulse_generator_counts(&g_device, &counts), KATHERINE_E_UNSUPPORTED);
    KT_CHECK_EQ(katherine_reset_pulse_generator_counts(&g_device), KATHERINE_E_UNSUPPORTED);

    fixture_stop();
}

// ------------------------------------------------------------------
// The control. A row that has the hardware is not refused, so the guards
// above are reading the count and not simply always refusing.

static void
test_a_row_with_the_hardware_is_not_refused(void)
{
    KT_REQUIRE(fixture_start(HW_GEN2));

    KT_CHECK(g_device.derived_info.all_gpio_count > 0);
    KT_CHECK(g_device.derived_info.tdc_channel_count > 0);
    KT_CHECK(g_device.derived_info.pulse_channel_count > 0);

    const katherine_gpio_config_t gpio = {0};
    KT_CHECK(katherine_set_gpio_config(&g_device, &gpio) != KATHERINE_E_UNSUPPORTED);

    katherine_tdc_counts_t counts;
    KT_CHECK(katherine_get_tdc_counts(&g_device, &counts) != KATHERINE_E_UNSUPPORTED);

    fixture_stop();
}

int
main(void)
{
    KT_RUN(test_the_zero_row_is_supported_and_empty);
    KT_RUN(test_a_row_with_no_gpio_refuses_gpio);
    KT_RUN(test_a_row_with_no_timing_channels_refuses_counts);
    KT_RUN(test_a_row_with_no_pulse_channels_refuses_the_generator);
    KT_RUN(test_a_row_with_the_hardware_is_not_refused);
    return kt_summary();
}
