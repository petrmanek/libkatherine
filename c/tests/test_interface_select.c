/**
 * \file
 * \brief Telling a second-generation readout which interface drives it.
 *
 * A second-generation readout that has not been told which interface drives
 * it polls them all, acknowledging a command in tens of milliseconds rather
 * than a fraction of one. That exceeds the control session's timeout and so
 * fails every acquisition start.
 *
 * What is asserted here is the decision and the datagram, against the
 * emulated readout: which readouts are told, which are not, that a caller can
 * tell one itself, and that the byte sent names Ethernet. The effect on the
 * readout's own latency is hardware behaviour and cannot be observed here,
 * the emulator modelling the protocol rather than the firmware's main loop.
 *
 * Addressing follows test_e2e_acq.c: device.c hard-codes local ports 1555
 * and 1556 on the wildcard address, so the mock readout is given the
 * secondary loopback address 127.0.0.2 and the two coexist through
 * SO_REUSEADDR. Where that does not hold -- 127.0.0.2 is not aliased by
 * default on macOS -- the mock never binds and the program reports a TAP
 * skip and exits 77.
 *
 * \author Petr Mánek
 * \date 14.10.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <katherine/katherine.h>
#include <katherine/emulator.h>

#include "ktest.h"
#include "kthread.h"
#include "protocol/cmd_builder.h"
#include "protocol/cmd_interface.h"

// The readout the device talks to. Its own ports are the ones device.c
// insists on, on an address of its own.
#define MOCK_ADDRESS        "127.0.0.2"
/* The same readout as a device address, which is what the public API takes. */
#define MOCK_DEVICE_ADDRESS "udp://" MOCK_ADDRESS
#define MOCK_CMD_PORT       1555

// Long enough that the mock outlives an open, short enough that its thread
// ends promptly once the open is done: the serve loop below exits on the
// first receive that times out.
#define MOCK_TIMEOUT_MS     300

// Hardware types from the recognition table in c/src/device/device.c.
#define HW_TYPE_GEN1        0x01
#define HW_TYPE_GEN2        0x03

static katherine_emu_t g_emu;
static katherine_udp_t g_mock;

// Answers commands out of the emulated readout until the device stops asking.
//
// Replies go to a fixed address rather than to whoever asked last. The device
// binds its control socket to the wildcard, so the source address its commands
// carry is whatever the host picks for the route: 127.0.0.1 on Linux, and on
// macOS 127.0.0.2 -- this mock's own address, since that is where the command
// was addressed. Answering "whoever asked last" there means answering itself,
// which it then reads as another command and answers again, with no idle
// moment for the receive timeout to end the loop. Measured: the mock took
// 52404 of its own datagrams in six seconds and the test never returned.
//
// Pinning the session instead would discard those same commands, for the same
// reason: the address to answer and the address they arrive from differ.
static void *
mock_serve(void *arg)
{
    (void) arg;

    const struct sockaddr_in reply_to = g_mock.address_remote;

    for (;;) {
        katherine_cmd_t cmd;
        if (katherine_udp_recv_exact(&g_mock, cmd.b, sizeof(cmd.b)) != 0) break;

        g_mock.address_remote = reply_to;

        if (katherine_emu_cmd_in(&g_emu, cmd.b, sizeof(cmd.b)) != 0) continue;

        uint8_t crd[KATHERINE_EMU_CRD_SIZE];
        size_t len = 0;
        while (katherine_emu_crd_out(&g_emu, crd, &len) == KATHERINE_E_OK) {
            (void) katherine_udp_send_exact(&g_mock, crd, len);
        }
    }

    return NULL;
}

// Opens a device against a mock readout of the given hardware type, runs \p
// after_open on it if given, and returns how many interface selections the
// session sent, or -1 if the open failed. The payload of the last one is
// written through \p payload, and what after_open returned through \p
// after_res.
static int
selections_during_session(uint8_t hw_type, uint32_t flags, uint32_t *payload,
    katherine_error_t (*after_open)(katherine_device_t *), katherine_error_t *after_res)
{
    katherine_emu_profile_t profile;
    katherine_device_t device;
    kthread_t thread;
    int count = 0;

    katherine_emu_profile_defaults(&profile);
    profile.hw_type = hw_type;
    if (katherine_emu_init(&g_emu, &profile) != KATHERINE_E_OK) return -1;

    if (kthread_start(&thread, mock_serve, NULL) != 0) {
        katherine_emu_fini(&g_emu);
        return -1;
    }

    const katherine_error_t res = katherine_device_init(&device, MOCK_DEVICE_ADDRESS, flags);
    if (res == KATHERINE_E_OK) {
        if (after_open != NULL) {
            const katherine_error_t sub = after_open(&device);
            if (after_res != NULL) *after_res = sub;
        }
        katherine_device_fini(&device);
    }

    (void) kthread_join(&thread);

    katherine_emu_log_entry_t log[32];
    const size_t n = katherine_emu_log_read(&g_emu, log, sizeof(log) / sizeof(log[0]));
    for (size_t i = 0; i < n; ++i) {
        if (log[i].opcode == (uint8_t) CMD_TYPE_INTERFACE_SELECTION) {
            ++count;
            if (payload != NULL) *payload = log[i].payload;
        }
    }

    katherine_emu_fini(&g_emu);
    return res == KATHERINE_E_OK ? count : -1;
}

// a) A second-generation readout is told, once, and told Ethernet -- the
// only interface this library speaks.
static void
test_a_the_second_generation_is_told_which_interface(void)
{
    uint32_t payload = 0xFFFFFFFFu;

    KT_CHECK_EQ(selections_during_session(HW_TYPE_GEN2, 0, &payload, NULL, NULL), 1);
    KT_CHECK_EQ(payload, 0u);
}

// b) So is the first generation, which has no such command and drops the
// datagram. Opening does not know the generation when it sends this and does
// not need to: in both first-generation firmwares the dispatcher sends opcode
// 0x50 to the same do-nothing exit as any unknown opcode, restoring the stack
// and returning without a reply. Read out of the binaries.
//
// Asking first would cost what asking is meant to save -- every exchange
// before the selection runs about 69 ms against a 100 ms budget, and about
// 0.14 ms after it.
static void
test_b_the_first_generation_is_told_too_and_discards_it(void)
{
    KT_CHECK_EQ(selections_during_session(HW_TYPE_GEN1, 0, NULL, NULL, NULL), 1);
}

// c) And a caller that says not to, is obeyed.
static void
test_c_the_flag_suppresses_it(void)
{
    KT_CHECK_EQ(selections_during_session(HW_TYPE_GEN2, KATHERINE_DEVICE_DONT_SELECT_INTERFACE, NULL, NULL, NULL), 0);
}

// d) And suppressing the probe does not suppress this, the two having stopped
// being related: the selection is sent before the probe and without reference
// to what it would find. KATHERINE_DEVICE_DONT_SELECT_INTERFACE is the only
// thing that holds it back.
static void
test_d_an_unprobed_readout_is_told_anyway(void)
{
    KT_CHECK_EQ(selections_during_session(HW_TYPE_GEN2, KATHERINE_DEVICE_DONT_ENUMERATE, NULL, NULL, NULL), 1);
}

static katherine_error_t
select_the_link(katherine_device_t *device)
{
    return katherine_device_select_interface(device);
}

// e) A caller that suppressed the step at open can take it itself, and gets
// the same datagram.
static void
test_e_a_caller_can_select_the_interface_itself(void)
{
    katherine_error_t res = KATHERINE_E_STATE; // any non-OK: unset if after_open never ran
    uint32_t payload      = 0xFFFFFFFFu;

    KT_CHECK_EQ(selections_during_session(
                    HW_TYPE_GEN2, KATHERINE_DEVICE_DONT_SELECT_INTERFACE, &payload, select_the_link, &res),
        1);
    KT_CHECK_EQ(res, KATHERINE_E_OK);
    KT_CHECK_EQ(payload, 0u);
}

int
main(void)
{
    if (katherine_udp_init_bound(&g_mock, MOCK_ADDRESS, MOCK_CMD_PORT, "127.0.0.1", MOCK_CMD_PORT,
            MOCK_TIMEOUT_MS)
        != 0) {
        printf("1..0 # SKIP cannot bind %s:%d, which this host may not alias\n", MOCK_ADDRESS, MOCK_CMD_PORT);
        return 77;
    }

    KT_RUN(test_a_the_second_generation_is_told_which_interface);
    KT_RUN(test_b_the_first_generation_is_told_too_and_discards_it);
    KT_RUN(test_c_the_flag_suppresses_it);
    KT_RUN(test_d_an_unprobed_readout_is_told_anyway);
    KT_RUN(test_e_a_caller_can_select_the_interface_itself);

    katherine_udp_fini(&g_mock);
    return kt_summary();
}
