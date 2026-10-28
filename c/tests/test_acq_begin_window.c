/**
 * \file
 * \brief The window katherine_acquisition_begin() sets its state in.
 *
 * begin() used to mark the acquisition RUNNING after
 * katherine_tpx3_set_acq_mode() returned, with the control lock, a flush
 * and the start command still to go. A failure in that window left an
 * acquisition claiming to run against a readout that was never started,
 * and katherine_acquisition_read() tests exactly that state -- so it would
 * wait out fail_timeout plus the whole requested measurement for data
 * nobody had asked for. The assignment moved after the command.
 *
 * Nothing could test it. The window's only failures are a refused lock and
 * a refused send, and neither is something a peer can arrange: the start
 * command is unacknowledged, so a readout going silent does not fail it.
 * ksim's --mute-opcode cannot reach this, and the commit that made the fix
 * said so.
 *
 * kemu_transport.h can: the emulator behind a vtable in this process, with
 * the *link* refusing to carry one named command. No sockets on the
 * control plane, no second process, no threads, and the virtual clock only
 * moves when a call asks it to.
 *
 * \author Petr Mánek
 * \date 25.10.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <string.h>

#include <katherine/katherine.h>

#include "kemu_transport.h"
#include "ktest.h"

// The acquisition-start operation code, which is sent and never answered.
#define OPCODE_ACQUISITION_START 0x03

#define CONFIG_SIZE              (sizeof(katherine_tpx3_config_t) + sizeof(katherine_tpx3_chip_config_t))
#define MD_BUFFER_SIZE           (KATHERINE_TPX3_MD_SIZE * 4096)
#define PIXEL_BUFFER_BYTES       (1024 * sizeof(katherine_tpx3_px_f_toa_tot_t))
#define REPORT_TIMEOUT_MS        100
#define FAIL_TIMEOUT_MS          1000

static katherine_device_t g_device;
static kemu_transport_t g_link;

// A second-generation readout, which is what the acquisition path dispatches
// on, and otherwise the emulator's defaults.
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

static katherine_tpx3_config_t *
make_config(void)
{
    katherine_tpx3_config_t *config = (katherine_tpx3_config_t *) calloc(1, CONFIG_SIZE);
    if (config == NULL) return NULL;

    config->common.chip_type   = KATHERINE_CHIP_TPX3;
    config->common.chip_count  = 1;
    config->common.acq_time    = 400000.0;
    config->common.frame_count = 1;

    config->px_mode         = KATHERINE_TPX3_PX_TOA_TOT;
    config->fast_vco_enable = true;
    config->clock_phase     = KATHERINE_TPX3_PHASE_1;
    config->clock_frequency = KATHERINE_TPX3_FREQ_40_MHZ;

    return config;
}

// ------------------------------------------------------------------

static void
test_a_begin_that_reaches_the_start_command_runs(void)
{
    KT_REQUIRE(fixture_start());

    katherine_tpx3_config_t *config = make_config();
    KT_REQUIRE(config != NULL);

    katherine_acquisition_t acq;
    KT_REQUIRE(katherine_acquisition_init(
                   &acq, &g_device, NULL, MD_BUFFER_SIZE, PIXEL_BUFFER_BYTES, REPORT_TIMEOUT_MS, FAIL_TIMEOUT_MS)
        == KATHERINE_E_OK);

    KT_CHECK_EQ(katherine_acquisition_begin(&acq, config, true), KATHERINE_E_OK);
    KT_CHECK_EQ((int) acq.state, (int) KATHERINE_ACQUISITION_STATE_RUNNING);

    (void) katherine_acquisition_abort(&acq);
    katherine_acquisition_fini(&acq);
    free(config);
    fixture_stop();
}

// The window itself: everything up to the start command is carried, and the
// start command is refused by the link.
static void
test_a_start_the_link_refuses_leaves_it_not_running(void)
{
    KT_REQUIRE(fixture_start());
    g_link.fail_send_opcode = OPCODE_ACQUISITION_START;
    g_link.fail_send_code   = KATHERINE_E_IO;

    katherine_tpx3_config_t *config = make_config();
    KT_REQUIRE(config != NULL);

    katherine_acquisition_t acq;
    KT_REQUIRE(katherine_acquisition_init(
                   &acq, &g_device, NULL, MD_BUFFER_SIZE, PIXEL_BUFFER_BYTES, REPORT_TIMEOUT_MS, FAIL_TIMEOUT_MS)
        == KATHERINE_E_OK);

    KT_CHECK_EQ(katherine_acquisition_begin(&acq, config, true), KATHERINE_E_IO);

    // The failure was the start command and nothing earlier.
    KT_CHECK_EQ((int) g_link.failures, 1);

    // And this is the assignment the fix moved. RUNNING here is the
    // regression.
    KT_CHECK_EQ((int) acq.state, (int) KATHERINE_ACQUISITION_STATE_NOT_STARTED);

    katherine_acquisition_fini(&acq);
    free(config);
    fixture_stop();
}

// Why the state matters beyond tidiness: the retry a caller makes after a
// failed start has to be a retry, not a second begin on something that
// already calls itself running.
static void
test_the_retry_after_a_refused_start_succeeds(void)
{
    KT_REQUIRE(fixture_start());
    g_link.fail_send_opcode = OPCODE_ACQUISITION_START;
    g_link.fail_send_code   = KATHERINE_E_IO;

    katherine_tpx3_config_t *config = make_config();
    KT_REQUIRE(config != NULL);

    katherine_acquisition_t acq;
    KT_REQUIRE(katherine_acquisition_init(
                   &acq, &g_device, NULL, MD_BUFFER_SIZE, PIXEL_BUFFER_BYTES, REPORT_TIMEOUT_MS, FAIL_TIMEOUT_MS)
        == KATHERINE_E_OK);

    KT_CHECK(katherine_acquisition_begin(&acq, config, true) != KATHERINE_E_OK);

    g_link.fail_send_opcode = 0;
    KT_CHECK_EQ(katherine_acquisition_begin(&acq, config, true), KATHERINE_E_OK);
    KT_CHECK_EQ((int) acq.state, (int) KATHERINE_ACQUISITION_STATE_RUNNING);

    (void) katherine_acquisition_abort(&acq);
    katherine_acquisition_fini(&acq);
    free(config);
    fixture_stop();
}

// A pixel buffer the decode loop would write past. katherine_acquisition_init()
// takes the size but not the mode, so it cannot tell how large a hit is; begin()
// knows both. One hit of this mode is 16 bytes.
static void
test_a_pixel_buffer_under_one_hit_is_refused(void)
{
    KT_REQUIRE(fixture_start());

    katherine_tpx3_config_t *config = make_config();
    KT_REQUIRE(config != NULL);

    const size_t hit = sizeof(katherine_tpx3_px_f_toa_tot_t);

    katherine_acquisition_t acq;
    KT_REQUIRE(katherine_acquisition_init(
                   &acq, &g_device, NULL, MD_BUFFER_SIZE, hit - 1, REPORT_TIMEOUT_MS, FAIL_TIMEOUT_MS)
        == KATHERINE_E_OK);
    KT_CHECK_EQ(katherine_acquisition_begin(&acq, config, true), KATHERINE_E_INVAL);
    katherine_acquisition_fini(&acq);

    // And a raw stream writes no hits, so it needs no room for one.
    KT_REQUIRE(katherine_acquisition_init(
                   &acq, &g_device, NULL, MD_BUFFER_SIZE, 0, REPORT_TIMEOUT_MS, FAIL_TIMEOUT_MS)
        == KATHERINE_E_OK);
    KT_CHECK_EQ(katherine_acquisition_begin(&acq, config, false), KATHERINE_E_OK);
    (void) katherine_acquisition_abort(&acq);
    katherine_acquisition_fini(&acq);

    // Exactly one hit is enough, which is the boundary.
    KT_REQUIRE(katherine_acquisition_init(
                   &acq, &g_device, NULL, MD_BUFFER_SIZE, hit, REPORT_TIMEOUT_MS, FAIL_TIMEOUT_MS)
        == KATHERINE_E_OK);
    KT_CHECK_EQ(katherine_acquisition_begin(&acq, config, true), KATHERINE_E_OK);
    (void) katherine_acquisition_abort(&acq);
    katherine_acquisition_fini(&acq);

    free(config);
    fixture_stop();
}

int
main(void)
{
    KT_RUN(test_a_begin_that_reaches_the_start_command_runs);
    KT_RUN(test_a_start_the_link_refuses_leaves_it_not_running);
    KT_RUN(test_the_retry_after_a_refused_start_succeeds);
    KT_RUN(test_a_pixel_buffer_under_one_hit_is_refused);
    return kt_summary();
}
