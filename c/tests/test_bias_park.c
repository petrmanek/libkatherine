/**
 * \file
 * \brief The bias failsafe on close, and the one flag that waives it.
 *
 * katherine_device_fini() parks every bias supply before it lets go of the
 * readout, so that a session ending -- cleanly, on an error, or by falling
 * out of a `with` block -- does not leave a sensor biased. That behaviour
 * was measured on a second-generation readout and had no test at all, which
 * left the two things this file asserts free to regress in silence: that
 * the park happens at all, and that it happens *before* the close decides
 * whether it may proceed.
 *
 * The second is the point. A close that finds an acquisition still live
 * refuses and leaks the link, and that refusal used to come first -- so the
 * one close most likely to be a mistake was also the one that left the
 * sensor live.
 *
 * The emulator keeps a single bias register rather than one per supply, so
 * what is asserted here is the value the last command carried. That it
 * reaches every supply is the loop's business and is read, not measured.
 *
 * \author Petr Mánek
 * \date 28.10.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include <katherine/katherine.h>

#include "device/private.h"

#include "kemu_transport.h"
#include "ktest.h"

#define MD_BUFFER_SIZE     (KATHERINE_TPX3_MD_SIZE * 4096)
#define PIXEL_BUFFER_BYTES (1024 * sizeof(katherine_tpx3_px_f_toa_tot_t))

// A bias no failsafe would arrive at by accident, and inside the supply
// range the second-generation row declares.
#define STANDING_VOLTS     42.0f

static katherine_device_t g_device;
static kemu_transport_t g_link;

// Second generation, which the device row credits with two bias supplies --
// so the park runs its loop more than once.
static bool
fixture_start(void)
{
    katherine_emu_profile_t profile;
    katherine_emu_profile_defaults(&profile);
    profile.hw_type     = 0x03;
    profile.hw_revision = 3;
    profile.fw_version  = 5;

    if (kemu_device(&g_device, &g_link, &profile) != KATHERINE_E_OK) return false;
    return g_device.derived_info.bias_supply_count >= 2;
}

// ------------------------------------------------------------------

static void
test_an_ordinary_close_parks_the_supplies(void)
{
    KT_REQUIRE(fixture_start());

    KT_REQUIRE(katherine_set_bias(&g_device, 0, STANDING_VOLTS) == KATHERINE_E_OK);
    KT_REQUIRE(g_link.emu.regs.bias == STANDING_VOLTS);

    katherine_device_fini(&g_device);
    KT_CHECK_EXACT(g_link.emu.regs.bias, KATHERINE_BIAS_PARK_VOLTS);

    kemu_device_fini(&g_device, &g_link);
}

// The failsafe runs before the refusal, not after it. A device still holding
// an acquisition keeps its link -- that is the documented outcome -- but the
// sensor is parked on the way out regardless.
static void
test_a_close_that_refuses_still_parks(void)
{
    KT_REQUIRE(fixture_start());

    katherine_acquisition_t acq;
    KT_REQUIRE(katherine_acquisition_init(&acq, &g_device, NULL, MD_BUFFER_SIZE, PIXEL_BUFFER_BYTES, 10, 10)
        == KATHERINE_E_OK);

    KT_REQUIRE(katherine_set_bias(&g_device, 0, STANDING_VOLTS) == KATHERINE_E_OK);

    // Reports on stderr and leaves the link standing, by design.
    katherine_device_fini(&g_device);

    KT_CHECK_EXACT(g_link.emu.regs.bias, KATHERINE_BIAS_PARK_VOLTS);

    // The refusal did happen: the link is the acquisition's to finish with.
    KT_CHECK(g_device.control_transport.ops != NULL);
    KT_CHECK(g_device.acquisition == &acq);

    katherine_acquisition_fini(&acq);
    katherine_device_fini(&g_device);
    kemu_device_fini(&g_device, &g_link);
}

// The waiver. An assembly whose bias must not be cycled -- a CdTe sensor,
// whose polarization takes hours to recover -- is opened with this and the
// close leaves the supply where the caller put it.
static void
test_danger_keep_bias_leaves_the_supply_standing(void)
{
    KT_REQUIRE(fixture_start());

    // kemu_device() installs a transport rather than opening one, so the
    // flags a real katherine_device_init() would have recorded are set here.
    katherine_device_state(&g_device)->flags = KATHERINE_DEVICE_DANGER_KEEP_BIAS;

    KT_REQUIRE(katherine_set_bias(&g_device, 0, STANDING_VOLTS) == KATHERINE_E_OK);

    katherine_device_fini(&g_device);
    KT_CHECK_EXACT(g_link.emu.regs.bias, STANDING_VOLTS);

    kemu_device_fini(&g_device, &g_link);
}

int
main(void)
{
    KT_RUN(test_an_ordinary_close_parks_the_supplies);
    KT_RUN(test_a_close_that_refuses_still_parks);
    KT_RUN(test_danger_keep_bias_leaves_the_supply_standing);
    return kt_summary();
}
