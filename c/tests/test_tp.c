/**
 * @file
 * @brief Validation tests for the libkatherine test pulse feature.
 *
 * 1. Pixel test-bit helpers vs. the proven BMC/BPC loader packing.
 * 2. EINVAL validation rules of katherine_set_test_pulses.
 * 3. Byte-exact 0x26 datagram check against the vendor C# reference,
 *    using a mock readout on a localhost UDP socket.
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// katherine/katherine.h must precede kthread.h: on Windows it transitively
// pulls in <winsock2.h> (via udp_win.h) ahead of the <windows.h> that
// kthread.h includes, and the reverse order does not compile under the
// Windows SDK. Same reasoning as test_e2e_acq.c's ordering of kspawn.h.
#include <katherine/katherine.h>

#include "kthread.h"
#include "ktest.h"

#define CO(X, Y) ((katherine_coord_t) {.x = (uint8_t) (X), .y = (uint8_t) (Y)})

/* ------------------------------------------------------------------ */
/* Test 1: pixel test-bit helpers                                      */

static void
test_px_helpers(void)
{
    static const int coords[][2] = {
        {0, 0},
        {255, 255},
        {0, 255},
        {255, 0},
        {7, 3},
        {120, 124},
        {128, 128},
    };
    static const int n_coords             = sizeof(coords) / sizeof(coords[0]);
    static const unsigned char reverse4[] = {
        0,
        8,
        4,
        12,
        2,
        10,
        6,
        14,
        1,
        9,
        5,
        13,
        3,
        11,
        7,
        15,
    };

    /* Cross-validate against the BMC loader: a BMC byte at file index
       y*256 + x carries mask (bit 0), loc_thl (bits 1-4), test (bit 5). */
    katherine_bmc_t *bmc = calloc(1, sizeof(katherine_bmc_t));
    KT_REQUIRE(bmc != NULL);
    for (int i = 0; i < n_coords; ++i) {
        const unsigned char loc_thl = (unsigned char) (i + 9) & 0xF;
        bmc->px_config[coords[i][1] * 256 + coords[i][0]] =
            (unsigned char) (0x20 | (loc_thl << 1) | 0x01);
    }

    katherine_px_config_t loaded;
    KT_CHECK(katherine_px_config_load_bmc_data(&loaded, bmc) == 0);

    /* The same pixels set through the helpers on an empty matrix must
       produce a bit-identical configuration. */
    katherine_px_config_t manual;
    memset(&manual, 0, sizeof(manual));
    for (int i = 0; i < n_coords; ++i) {
        katherine_px_config_set_test_bit(&manual, CO(coords[i][0], coords[i][1]), true);
        katherine_px_config_set_mask_bit(&manual, CO(coords[i][0], coords[i][1]), true);
        katherine_px_config_set_loc_thl(&manual, CO(coords[i][0], coords[i][1]), (i + 9) & 0xF);
    }
    KT_CHECK(memcmp(&loaded, &manual, sizeof(loaded)) == 0);

    /* The BPC loader stores loc_thl in reversed bit order; mask and test
       bits are format-invariant. */
    katherine_bpc_t *bpc = calloc(1, sizeof(katherine_bpc_t));
    KT_REQUIRE(bpc != NULL);
    for (int i = 0; i < n_coords; ++i) {
        const unsigned char loc_thl = (unsigned char) (i + 9) & 0xF;
        bpc->px_config[coords[i][1] * 256 + coords[i][0]] =
            (unsigned char) (0x20 | (reverse4[loc_thl] << 1) | 0x01);
    }
    katherine_px_config_t loaded_bpc;
    KT_CHECK(katherine_px_config_load_bpc_data(&loaded_bpc, bpc) == 0);
    KT_CHECK(memcmp(&loaded_bpc, &manual, sizeof(loaded_bpc)) == 0);

    /* Getter round-trip, exact bit location and clearing. */
    for (int i = 0; i < n_coords; ++i) {
        KT_CHECK(katherine_px_config_get_test_bit(&manual, CO(coords[i][0], coords[i][1])));
        KT_CHECK(katherine_px_config_get_mask_bit(&manual, CO(coords[i][0], coords[i][1])));
        KT_CHECK(
            katherine_px_config_get_loc_thl(&manual, CO(coords[i][0], coords[i][1])) == (unsigned) ((i + 9) & 0xF));
    }
    int n_test = 0, n_mask = 0, n_thl = 0;
    for (int x = 0; x < 256; ++x) {
        for (int y = 0; y < 256; ++y) {
            n_test += katherine_px_config_get_test_bit(&manual, CO(x, y));
            n_mask += katherine_px_config_get_mask_bit(&manual, CO(x, y));
            n_thl += katherine_px_config_get_loc_thl(&manual, CO(x, y)) != 0;
        }
    }
    KT_CHECK(n_test == n_coords);
    KT_CHECK(n_mask == n_coords);
    KT_CHECK(n_thl == n_coords);

    katherine_px_config_set_test_bit(&manual, CO(7, 3), false);
    KT_CHECK(!katherine_px_config_get_test_bit(&manual, CO(7, 3)));
    katherine_px_config_set_mask_bit(&manual, CO(7, 3), false);
    KT_CHECK(!katherine_px_config_get_mask_bit(&manual, CO(7, 3)));
    katherine_px_config_set_loc_thl(&manual, CO(7, 3), 0);
    KT_CHECK(katherine_px_config_get_loc_thl(&manual, CO(7, 3)) == 0);

    /* The helpers must not disturb the other per-pixel bits. */
    katherine_px_config_t nonzero, all_ff;
    memset(&nonzero, 0xFF, sizeof(nonzero));
    memset(&all_ff, 0xFF, sizeof(all_ff));
    katherine_px_config_set_test_bit(&nonzero, CO(42, 17), false);
    KT_CHECK(!katherine_px_config_get_test_bit(&nonzero, CO(42, 17)));
    KT_CHECK(katherine_px_config_get_mask_bit(&nonzero, CO(42, 17)));
    KT_CHECK(katherine_px_config_get_loc_thl(&nonzero, CO(42, 17)) == 15);
    katherine_px_config_set_test_bit(&nonzero, CO(42, 17), true);
    katherine_px_config_set_loc_thl(&nonzero, CO(42, 17), 5);
    KT_CHECK(katherine_px_config_get_loc_thl(&nonzero, CO(42, 17)) == 5);
    KT_CHECK(katherine_px_config_get_test_bit(&nonzero, CO(42, 17)));
    KT_CHECK(katherine_px_config_get_mask_bit(&nonzero, CO(42, 17)));
    katherine_px_config_set_loc_thl(&nonzero, CO(42, 17), 15);
    KT_CHECK(memcmp(&nonzero, &all_ff, sizeof(nonzero)) == 0);

    free(bmc);
    free(bpc);
}

/* ------------------------------------------------------------------ */
/* Test 2: parameter validation (device is never touched on EINVAL)    */

static void
test_validation(void)
{
    katherine_test_pulse_config_t tp = {
        .enabled      = true,
        .digital_only = false,
        .external     = false,
        .count        = 100,
        .period       = 6401,
        .phase        = 0,
    };

    katherine_test_pulse_config_t bad;

    bad       = tp;
    bad.count = 0;
    KT_CHECK(katherine_set_test_pulses(NULL, &bad) == EINVAL);

    bad        = tp;
    bad.period = 64;
    KT_CHECK(katherine_set_test_pulses(NULL, &bad) == EINVAL);

    bad        = tp;
    bad.period = 16322;
    KT_CHECK(katherine_set_test_pulses(NULL, &bad) == EINVAL);

    bad       = tp;
    bad.phase = 16;
    KT_CHECK(katherine_set_test_pulses(NULL, &bad) == EINVAL);
}

/* ------------------------------------------------------------------ */
/* Test 3: byte-exact datagram check against a mock readout            */

/* katherine_set_test_pulses() sends the command and then blocks in a
   retrying wait for its acknowledgement (config.c), all inside one opaque
   library call: the test cannot step in between the send and the receive
   the way the inline ping-pong of bench_udp's run_cmd_roundtrips() does, so
   the mock readout that supplies the acknowledgement genuinely has to run
   on its own thread, concurrently with that call, rather than lockstep in
   this one. */

/* MOCK_TIMEOUT_MS bounds how long the mock readout waits for the command: on
   the happy path it arrives within microseconds of katherine_set_test_pulses
   sending it, so this is pure headroom against scheduling jitter, not a
   value the test ever expects to actually wait out. */
#define MOCK_TIMEOUT_MS 5000

static katherine_udp_t mock_endpoint;
static unsigned char mock_captured[8];

static void *
mock_readout(void *arg)
{
    (void) arg;

    size_t n = sizeof(mock_captured);
    int res  = katherine_udp_recv(&mock_endpoint, mock_captured, &n);
    if (res != 0 || n != 8) {
        fprintf(stderr, "mock readout: unexpected datagram (res=%d, size=%zu)\n", res, n);
        exit(1);
    }

    /* Acknowledge: echo the command id with zero response data. The mock
       endpoint is never pinned, so katherine_udp_recv() above already
       retargeted its remote address at whoever just sent the command --
       exactly the server behavior katherine_udp_pin_remote() documents as
       the unpinned default. */
    unsigned char ack[8] = {0};
    ack[6]               = mock_captured[6];
    (void) katherine_udp_send_exact(&mock_endpoint, ack, sizeof(ack));
    return NULL;
}

static void
send_and_capture(katherine_device_t *device, const katherine_test_pulse_config_t *tp,
    const unsigned char expected[8])
{
    kthread_t thread;
    memset(mock_captured, 0xAA, sizeof(mock_captured));
    KT_REQUIRE(kthread_start(&thread, mock_readout, NULL) == 0);

    KT_CHECK(katherine_set_test_pulses(device, tp) == 0);
    KT_CHECK(kthread_join(&thread) == 0);

    KT_CHECK_MEM_EQ(mock_captured, expected, 8);
}

static void
test_datagram(void)
{
    static const uint16_t MOCK_PORT  = 45679;
    static const uint16_t LOCAL_PORT = 45678;

    /* Mock readout endpoint, over the public katherine_udp_* API alone. */
    KT_REQUIRE(
        katherine_udp_init_bound(&mock_endpoint, "127.0.0.1", MOCK_PORT, "127.0.0.1", LOCAL_PORT, MOCK_TIMEOUT_MS)
        == 0);

    /* Device whose control socket points at the mock. A failed init leaves
       device.control_socket in an indeterminate state (its mutex is never
       constructed on this path), so bail out of the whole suite rather
       than risk destroying it later via katherine_udp_fini. */
    katherine_device_t device;
    KT_REQUIRE(katherine_udp_init(&device.control_socket, LOCAL_PORT, "127.0.0.1", MOCK_PORT, 2000) == 0);

    /* Reference case from the vendor C# TPSetting implementation:
       100 pulses, period 65 cycles (register 1), phase 0, enabled,
       analog, internal -> {100, 0, 1, 0, 4, 0, 0x26, 0}. */
    katherine_test_pulse_config_t tp = {
        .enabled      = true,
        .digital_only = false,
        .external     = false,
        .count        = 100,
        .period       = 65,
        .phase        = 0,
    };
    send_and_capture(&device, &tp, (const unsigned char[8]) {100, 0, 1, 0, 0x04, 0, 0x26, 0});

    /* The example configuration: 100 pulses, period 6401 cycles (register
       100 = 6400/64), phase 0. */
    tp.period = 6401;
    send_and_capture(&device, &tp, (const unsigned char[8]) {100, 0, 100, 0, 0x04, 0, 0x26, 0});

    /* Wide count (LE split), digital, phase 5; period 16321 is the
       maximum (register 255). */
    tp.count        = 0xBEEF;
    tp.period       = 16321;
    tp.phase        = 5;
    tp.digital_only = true;
    send_and_capture(&device, &tp, (const unsigned char[8]) {0xEF, 0xBE, 255, 5, 0x05, 0, 0x26, 0});

    /* External source: count/period/phase do not apply -- neither
       validated (out-of-range values below) nor transmitted. */
    tp.count    = 0;
    tp.period   = 0;
    tp.phase    = 99;
    tp.external = true;
    send_and_capture(&device, &tp, (const unsigned char[8]) {0, 0, 0, 0, 0x07, 0, 0x26, 0});

    /* Disabled: deterministic all-zero payload regardless of other fields. */
    tp.enabled = false;
    send_and_capture(&device, &tp, (const unsigned char[8]) {0, 0, 0, 0, 0, 0, 0x26, 0});

    katherine_udp_fini(&device.control_socket);
    katherine_udp_fini(&mock_endpoint);
}

int
main(void)
{
    KT_RUN(test_px_helpers);
    KT_RUN(test_validation);
    KT_RUN(test_datagram);
    return kt_summary();
}
