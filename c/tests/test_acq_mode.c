/**
 * @file
 * @brief Byte-exact vectors for the acquisition-mode command and its flush.
 *
 * The acquisition-mode command is a GeneralConfig accessor: the readout
 * merges byte 0 into Op_mode [2:1] and byte 1 into Fast_lo_en [6] of its own
 * register image, and that image reaches the sensor only when a
 * sensor-config-registers flush follows. Reading the image back with
 * GET_BACK_READ_REGISTER on the GeneralConfig index shows the merge
 * directly: 0x58, 0x5a and 0x5c as the mode is swept with the oscillator on,
 * and 0x18 once byte 1 goes to zero.
 *
 * Both expectations below are the datagrams a vendor readout tool puts on the
 * wire, verbatim, in captures taken eight years apart:
 *
 *     00 01 00 00 00 00 09 00   mode 0, fast oscillator on
 *     00 00 00 00 00 00 07 00   flush, ~1 ms later
 *
 * That tool flushes after every acquisition-mode write, including writes that
 * do not go on to start an acquisition. Omitting the flush leaves the sensor
 * in whatever state the preceding configuration pushed -- Op_mode 0 with the
 * oscillator on -- which is why acquisitions once returned ToA+ToT data no
 * matter which mode was requested.
 *
 * The mock readout runs on its own thread for the reason test_tp.c documents:
 * the send and the acknowledgement wait sit inside one opaque library call,
 * so a lockstep ping-pong in this thread cannot get between them. Here it has
 * to serve two commands per call rather than one.
 *
 * @author Petr Mánek
 * @date 27.8.26
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <katherine/katherine.h>

#include "kthread.h"
#include "ktest.h"

/* Pure headroom against scheduling jitter, as in test_tp.c: on the happy path
   each command reaches the mock within microseconds. */
#define MOCK_TIMEOUT_MS    5000

/** Number of datagrams one katherine_set_acq_mode() call is expected to send. */
#define EXPECTED_DATAGRAMS 2

static katherine_udp_t mock_endpoint;
static unsigned char mock_captured[EXPECTED_DATAGRAMS][8];
static size_t mock_count;

static void *
mock_readout(void *arg)
{
    (void) arg;

    for (mock_count = 0; mock_count < EXPECTED_DATAGRAMS; ++mock_count) {
        size_t n = sizeof(mock_captured[mock_count]);
        int res  = katherine_udp_recv(&mock_endpoint, mock_captured[mock_count], &n);
        if (res != 0 || n != 8) {
            /* Stop rather than exit: a library that sends only the first
               command leaves the count short, and the assertions below name
               that failure better than an abort from this thread would. */
            fprintf(stderr, "mock readout: datagram %zu missing (res=%d, size=%zu)\n", mock_count, res, n);
            return NULL;
        }

        unsigned char ack[8] = {0};
        ack[6]               = mock_captured[mock_count][6];
        (void) katherine_udp_send_exact(&mock_endpoint, ack, sizeof(ack));
    }

    return NULL;
}

static void
set_and_capture(katherine_device_t *device, katherine_acquisition_mode_t acq_mode, bool fast_vco_enabled,
    const unsigned char expected_mode[8])
{
    /* The flush carries the sub-command number in byte 0, and
       CMD_START_SENSOR_CONFIG_REGISTERS_UPDATE is 0, so the whole datagram is
       zero but for the opcode. */
    static const unsigned char expected_flush[8] = {0, 0, 0, 0, 0, 0, 0x07, 0};

    kthread_t thread;
    memset(mock_captured, 0xAA, sizeof(mock_captured));
    mock_count = 0;
    KT_REQUIRE(kthread_start(&thread, mock_readout, NULL) == 0);

    KT_CHECK(katherine_set_acq_mode(device, acq_mode, fast_vco_enabled) == 0);
    KT_CHECK(kthread_join(&thread) == 0);

    KT_CHECK(mock_count == EXPECTED_DATAGRAMS);
    KT_CHECK_MEM_EQ(mock_captured[0], expected_mode, 8);
    KT_CHECK_MEM_EQ(mock_captured[1], expected_flush, 8);
}

static void
test_datagrams(void)
{
    static const uint16_t MOCK_PORT  = 45681;
    static const uint16_t LOCAL_PORT = 45680;

    KT_REQUIRE(
        katherine_udp_init_bound(&mock_endpoint, "127.0.0.1", MOCK_PORT, "127.0.0.1", LOCAL_PORT, MOCK_TIMEOUT_MS)
        == 0);

    /* A failed init leaves the control socket indeterminate -- its mutex is
       never constructed on that path -- so bail out rather than destroy it
       later, exactly as test_tp.c does. */
    katherine_device_t device;
    /* Zeroed whole: this device is built by hand rather than by
       katherine_device_init(), and katherine_device_t carries fields beyond
       the two sessions -- the borrowed acquisition among them, which
       katherine_device_fini() acts on. */
    memset(&device, 0, sizeof(device));
    KT_REQUIRE(katherine_udp_init(&device.control_socket, LOCAL_PORT, "127.0.0.1", MOCK_PORT, 2000) == 0);

    /* Mode in byte 0, oscillator flag in byte 1. The three mode vectors are
       the ones whose register readbacks were 0x58, 0x5a and 0x5c. */
    set_and_capture(&device, ACQUISITION_MODE_TOA_TOT, true, (const unsigned char[8]) {0x00, 0x01, 0, 0, 0, 0, 0x09, 0});
    set_and_capture(
        &device, ACQUISITION_MODE_ONLY_TOA, true, (const unsigned char[8]) {0x01, 0x01, 0, 0, 0, 0, 0x09, 0});
    set_and_capture(
        &device, ACQUISITION_MODE_EVENT_ITOT, true, (const unsigned char[8]) {0x02, 0x01, 0, 0, 0, 0, 0x09, 0});

    /* Oscillator off clears byte 1 and nothing else; byte 0 bit 7 stays
       clear, where an earlier encoding had wrongly put this flag. */
    set_and_capture(
        &device, ACQUISITION_MODE_TOA_TOT, false, (const unsigned char[8]) {0x00, 0x00, 0, 0, 0, 0, 0x09, 0});
    set_and_capture(
        &device, ACQUISITION_MODE_EVENT_ITOT, false, (const unsigned char[8]) {0x02, 0x00, 0, 0, 0, 0, 0x09, 0});

    katherine_udp_fini(&device.control_socket);
    katherine_udp_fini(&mock_endpoint);
}

int
main(void)
{
    KT_RUN(test_datagrams);
    return kt_summary();
}
