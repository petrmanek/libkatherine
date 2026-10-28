/**
 * \file
 * \brief An acquisition whose configuration the readout never answers.
 *
 * What katherine_acquisition_begin() owes a caller when it fails is an
 * acquisition that is not RUNNING: katherine_acquisition_read() tests that
 * state, so an acquisition that claims it against a readout that was never
 * started makes the next read wait out fail_timeout plus the whole
 * requested measurement. Until the emulated readout could be told to go
 * silent it answered everything, and no failing begin() could be staged at
 * all.
 *
 * It can be told now: `--mute-opcode 0x02` drops the bias-settings command,
 * which katherine_tpx3_configure() sends and waits for an acknowledgement
 * of, as an operation code the dispatcher does not know already is. Both
 * cases below run the same acquisition; only the daemon's command line
 * differs.
 *
 * \note This does not reach the window the state assignment was moved out
 *   of, and nothing driven from the readout can. That window runs from the
 *   control lock to the start command, and the start command is
 *   unacknowledged -- `cmd_send6_i64`, no wait -- so a silent readout
 *   cannot fail it. Only a failed lock or a failed send can, neither of
 *   which a peer controls. Reaching it wants a transport stub backed by the
 *   in-process emulator, which is a larger piece of test machinery than
 *   this.
 *
 * \author Petr Mánek
 * \date 25.10.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// katherine/katherine.h must precede kspawn.h; see test_px_recovery.c, which
// documents the include order.
#include <katherine/katherine.h>

#include "kspawn.h"
#include "ktest.h"
#include "msleep.h"

#define KSR_STR_(x)                  #x
#define KSR_STR(x)                   KSR_STR_(x)

#define KSIM_LISTEN_ADDRESS          "127.0.0.2"
#define KSIM_DEVICE_ADDRESS          "udp://" KSIM_LISTEN_ADDRESS
#define EXPECTED_CHIP_ID             "A1-W0001"

// The bias-settings operation code, as tools/ksim/main.c takes it.
#define MUTE_BIAS                    "0x02"

// As in test_px_recovery.c: the device's control timeout is 100 ms, so 80
// attempts bound the wait at roughly ten seconds.
#define READY_ATTEMPTS               80
#define READY_SLEEP_MS               25

#define SHORT_FRAME_DURATION_SECONDS 0.0004
#define MD_BUFFER_SIZE               (KATHERINE_TPX3_MD_SIZE * 4096)
#define PIXEL_BUFFER_HITS            1024
#define REPORT_TIMEOUT_MS            500

// Short, because the point of the muted case is that it fails promptly: a
// command that goes unanswered must be reported by begin() and not waited
// out by the read loop.
#define FAIL_TIMEOUT_MS              2000

#define CONFIG_SIZE_1                (sizeof(katherine_tpx3_config_t) + sizeof(katherine_tpx3_chip_config_t))

static kspawn_proc_t g_ksim = {0};
static katherine_device_t g_device;
static bool g_device_open = false;
static char g_error[256];

// ------------------------------------------------------------------

// Spawns the daemon, muting the bias-settings command when asked, and waits for the
// readout to answer a chip-identifier request with its own identifier. The
// readiness loop and the reason it tears the device down between attempts
// are test_px_recovery.c's.
static const char *
fixture_start(const char *ksim_path, bool mute_bias)
{
    char *argv[] = {
        (char *) "ksim",
        (char *) "--listen",
        (char *) KSIM_LISTEN_ADDRESS,
        (char *) "--profile",
        (char *) "gen2-tpx3",
        (char *) "--quiet",
        // Filled in below, or left as the terminator.
        NULL,
        NULL,
        NULL,
    };

    if (mute_bias) {
        size_t i    = sizeof(argv) / sizeof(argv[0]) - 3;
        argv[i]     = (char *) "--mute-opcode";
        argv[i + 1] = (char *) MUTE_BIAS;
    }

    int res = kspawn_start(&g_ksim, ksim_path, argv);
    if (res != 0) {
        snprintf(g_error, sizeof(g_error), "cannot spawn '%s': %s", ksim_path, strerror(res));
        return g_error;
    }

    char chip_id[KATHERINE_CHIP_ID_STR_SIZE];
    for (int attempt = 0; attempt < READY_ATTEMPTS; ++attempt) {
        res = katherine_device_init(&g_device, KSIM_DEVICE_ADDRESS, 0);
        if (res == KATHERINE_E_OK) {
            g_device_open = true;

            if (katherine_get_chip_id(&g_device, 0, chip_id) == 0 && strcmp(chip_id, EXPECTED_CHIP_ID) == 0) {
                if (katherine_device_enumerate(&g_device) != KATHERINE_E_OK) {
                    snprintf(g_error, sizeof(g_error), "readout answered its chip id but not its status");
                    return g_error;
                }

                return NULL;
            }

            katherine_device_fini(&g_device);
            g_device_open = false;
        }

        if (!kspawn_alive(&g_ksim)) {
            snprintf(g_error, sizeof(g_error),
                "ksim exited during startup: it could not bind " KSIM_LISTEN_ADDRESS
                " (not aliased by default on macOS), or could not be executed at all");
            return g_error;
        }

        katherine_msleep(READY_SLEEP_MS);
    }

    snprintf(g_error, sizeof(g_error), "no answer from ksim at " KSIM_LISTEN_ADDRESS ":1555 within %d attempts, last open %s",
        READY_ATTEMPTS, katherine_strerror(res));
    return g_error;
}

static void
fixture_stop(void)
{
    if (g_device_open) {
        katherine_device_fini(&g_device);
        g_device_open = false;
    }

    kspawn_stop(&g_ksim);
}

// A configuration equivalent to test_px_recovery.c's, less the DAC values
// the emulated readout does not act on.
static void
configure(katherine_tpx3_config_t *config)
{
    memset(config, 0, CONFIG_SIZE_1);

    config->common.chip_type              = KATHERINE_CHIP_TPX3;
    config->common.chip_count             = 1;
    config->common.frame_duration_seconds = SHORT_FRAME_DURATION_SECONDS;
    config->common.frame_count            = 1;
    config->common.bias[0]                = 230;

    config->px_mode         = KATHERINE_TPX3_PX_TOA_TOT;
    config->fast_vco_enable = true;

    config->clock_phase     = KATHERINE_TPX3_PHASE_1;
    config->clock_frequency = KATHERINE_TPX3_FREQ_40_MHZ;
}

// ------------------------------------------------------------------

static void
test_an_answered_configuration_leaves_the_acquisition_running(void)
{
    katherine_tpx3_config_t *storage = (katherine_tpx3_config_t *) calloc(1, CONFIG_SIZE_1);
    KT_REQUIRE(storage != NULL);
    configure(storage);

    katherine_acquisition_t acq;
    KT_REQUIRE(katherine_acquisition_init(&acq, &g_device, NULL, MD_BUFFER_SIZE,
                   PIXEL_BUFFER_HITS * sizeof(katherine_tpx3_px_f_toa_tot_t), REPORT_TIMEOUT_MS, FAIL_TIMEOUT_MS)
        == KATHERINE_E_OK);

    KT_CHECK(katherine_acquisition_begin(&acq, storage, true) == KATHERINE_E_OK);
    KT_CHECK_EQ((int) acq.state, (int) KATHERINE_ACQUISITION_STATE_RUNNING);

    (void) katherine_acquisition_abort(&acq);
    katherine_acquisition_fini(&acq);
    free(storage);
}

static void
test_an_unanswered_configuration_leaves_it_not_running(void)
{
    katherine_tpx3_config_t *storage = (katherine_tpx3_config_t *) calloc(1, CONFIG_SIZE_1);
    KT_REQUIRE(storage != NULL);
    configure(storage);

    katherine_acquisition_t acq;
    KT_REQUIRE(katherine_acquisition_init(&acq, &g_device, NULL, MD_BUFFER_SIZE,
                   PIXEL_BUFFER_HITS * sizeof(katherine_tpx3_px_f_toa_tot_t), REPORT_TIMEOUT_MS, FAIL_TIMEOUT_MS)
        == KATHERINE_E_OK);

    KT_CHECK(katherine_acquisition_begin(&acq, storage, true) != KATHERINE_E_OK);

    // The invariant this file exists for. A state of RUNNING here is the
    // regression: a caller that trusts it goes on to read a measurement
    // that was never started, and the state guard WP39 adds would refuse
    // the retry it would naturally make instead.
    KT_CHECK_EQ((int) acq.state, (int) KATHERINE_ACQUISITION_STATE_NOT_STARTED);

    katherine_acquisition_fini(&acq);
    free(storage);
}

int
main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <path-to-ksim>\n", argv[0]);
        return 2;
    }

    // One daemon per case, because the mute is a command-line option. The
    // first case is the positive control: without it, the same acquisition
    // starts and the state says so.
    const char *skip = fixture_start(argv[1], false);
    if (skip != NULL) {
        printf("1..0 # SKIP %s\n", skip);
        fixture_stop();
        return 77;
    }
    KT_RUN(test_an_answered_configuration_leaves_the_acquisition_running);
    fixture_stop();

    skip = fixture_start(argv[1], true);
    if (skip != NULL) {
        printf("1..0 # SKIP %s\n", skip);
        fixture_stop();
        return 77;
    }
    KT_RUN(test_an_unanswered_configuration_leaves_it_not_running);
    fixture_stop();

    return kt_summary();
}
