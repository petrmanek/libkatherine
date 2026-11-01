/**
 * \file
 * \brief The sensor self-test, and the one error code nothing could reach.
 *
 * katherine_tpx3_perform_digital_test() walks the chip through a fixed
 * number of matrix patterns and answers KATHERINE_E_HW_UNKNOWN when the
 * chip reports a different count: the sensor answered, but not with the
 * result asked for, and no enumerator names which pattern failed.
 *
 * That code was the last of the seventeen with no test behind it, and not
 * for want of trying -- the emulated readout hard-coded the digital test to
 * pass, so the failure could not be reached on this machine at all. The
 * profile now carries the answer, which is what these two cases turn on.
 *
 * \author Petr Mánek
 * \date 29.10.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include <katherine/katherine.h>

#include "kemu_transport.h"
#include "ktest.h"

static katherine_device_t g_device;
static kemu_transport_t g_link;

static bool
fixture_start(bool digital_test_ok)
{
    katherine_emu_profile_t profile;
    katherine_emu_profile_defaults(&profile);
    profile.hw_type         = 0x03;
    profile.hw_revision     = 3;
    profile.fw_version      = 5;
    profile.digital_test_ok = digital_test_ok;

    return kemu_device(&g_device, &g_link, &profile) == KATHERINE_E_OK;
}

// ------------------------------------------------------------------

static void
test_a_sensor_that_passes_answers_ok(void)
{
    KT_REQUIRE(fixture_start(true));

    KT_CHECK_EQ(katherine_tpx3_perform_digital_test(&g_device, 0), KATHERINE_E_OK);

    kemu_device_fini(&g_device, &g_link);
}

// The count is what the library tests, and any count but the full one is
// the same answer to it: the sensor replied, so the link and the command
// are fine, and nothing in the protocol says which pattern failed.
static void
test_a_sensor_that_fails_answers_hw_unknown(void)
{
    KT_REQUIRE(fixture_start(false));

    KT_CHECK_EQ(katherine_tpx3_perform_digital_test(&g_device, 0), KATHERINE_E_HW_UNKNOWN);

    // Not a transport failure dressed up as one: the same device answers
    // the next command normally.
    float temperature = 0.0f;
    KT_CHECK_EQ(katherine_get_sensor_temperature(&g_device, 0, &temperature), KATHERINE_E_OK);

    kemu_device_fini(&g_device, &g_link);
}

// And the code has a string, which is what a caller actually shows.
static void
test_hw_unknown_renders(void)
{
    const char *s = katherine_strerror(KATHERINE_E_HW_UNKNOWN);
    KT_CHECK(s != NULL);
    KT_CHECK(s[0] != '\0');
}

int
main(void)
{
    KT_RUN(test_a_sensor_that_passes_answers_ok);
    KT_RUN(test_a_sensor_that_fails_answers_hw_unknown);
    KT_RUN(test_hw_unknown_renders);
    return kt_summary();
}
