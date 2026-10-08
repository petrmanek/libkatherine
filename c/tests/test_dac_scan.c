/**
 * \file
 * \brief Reading the voltage a chip presents at a DAC monitor.
 *
 * The chip multiplexes one analog output and its own five-bit monitor index
 * selects what that output carries: 1 to 18 for the bias DACs, 28 to 31 for
 * the monitoring outputs. The monitors this library exposes are contiguous
 * over both, so the index that reaches the wire differs from the monitor the
 * caller named -- which is what these cases check, against a mock readout for
 * the envelope and against the emulated readout in memory for the answer.
 *
 * \author Petr Mánek
 * \date 18.9.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// katherine/katherine.h must precede kthread.h on Windows; see
// test_cmd_transact.c, which documents the include order.
#include <katherine/katherine.h>
#include <katherine/emulator.h>

#include "protocol/cmd_builder.h"
#include "protocol/cmd_interface.h"
#include "kthread.h"
#include "ktest.h"

#define HW_TYPE_GEN1      0x01
#define HW_TYPE_GEN2      0x03

// Chips a first- and a second-generation readout can address, from the table
// in c/src/device/device.c.
#define GEN1_CHIPS        1
#define GEN2_CHIPS        8

// Loopback endpoints of the mock readout; see test_bias.c for the other
// fixed sets in this tree.
#define PORT_DEVICE       42630
#define PORT_MOCK         42631
#define PORT_DEVICE_QUIET 42632
#define PORT_MOCK_QUIET   42633

#define MOCK_TIMEOUT_MS   5000
#define QUIET_TIMEOUT_MS  100
#define DEVICE_TIMEOUT_MS 2000

// What the mock answers with.
#define MOCK_ANSWER_V     0.8125f

// A DAC value to set and then read back at its monitor, and the volts per
// step the emulated readout scales it by (KATHERINE_EMU_DAC_SCAN_VOLT).
#define DAC_VALUE         137
#define EMU_VOLT_PER_STEP 0.001f

// The chip's own five-bit monitor index per DAC monitor, Timepix3 manual
// v2.0 Table 11: 1 to 18 for the DACs in katherine_tpx3_dac_t order, then 28
// to 31 for the four monitoring outputs. Written out rather than asked of the
// library, whose own mapping is what these cases check.
static const uint8_t TABLE_11[KATHERINE_TPX3_DAC_MONITOR_COUNT] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 28, 29, 30, 31};

static katherine_udp_t g_mock;
static katherine_udp_t g_mock_quiet;
static uint8_t g_captured[KATHERINE_CMD_CRD_SIZE];

static float
crd_float(const uint8_t *crd)
{
    float value;
    memcpy(&value, crd, sizeof(value));
    return value;
}

static int
mocks_init(void)
{
    int res = katherine_udp_init_bound(&g_mock, "127.0.0.1", PORT_MOCK, "127.0.0.1", PORT_DEVICE,
        MOCK_TIMEOUT_MS);
    if (res != 0) return res;

    res = katherine_udp_init_bound(&g_mock_quiet, "127.0.0.1", PORT_MOCK_QUIET, "127.0.0.1",
        PORT_DEVICE_QUIET, QUIET_TIMEOUT_MS);
    if (res != 0) katherine_udp_fini(&g_mock);

    return res;
}

static void
mocks_fini(void)
{
    katherine_udp_fini(&g_mock_quiet);
    katherine_udp_fini(&g_mock);
}

// Receives one command, records it whole, and answers it under the scan's own
// identifier.
static void *
mock_readout(void *arg)
{
    uint8_t crd[KATHERINE_CMD_CRD_SIZE] = {0};
    const float value                   = MOCK_ANSWER_V;

    (void) arg;

    if (katherine_udp_recv_exact(&g_mock, g_captured, sizeof(g_captured)) != 0) return NULL;

    memcpy(crd, &value, sizeof(value));
    crd[KATHERINE_CMD_OPCODE_BYTE] = (uint8_t) CMD_TYPE_INTERNAL_DAC_SCAN;
    (void) katherine_udp_send_exact(&g_mock, crd, sizeof(crd));

    return NULL;
}

static katherine_error_t
device_init(katherine_device_t *device, uint8_t hw_type, uint16_t local_port, uint16_t remote_port)
{
    memset(device, 0, sizeof(*device));

    if (hw_type != 0) {
        const katherine_device_info_t info = {.hw_type = hw_type};
        if (katherine_device_declare(device, &info) != KATHERINE_E_OK) return KATHERINE_E_INVAL;
    }

    return katherine_udp_init_bound(&device->control_socket, "127.0.0.1", local_port, "127.0.0.1",
        remote_port, DEVICE_TIMEOUT_MS);
}

static void
expect_nothing_sent(void)
{
    uint8_t buf[KATHERINE_CMD_CRD_SIZE];
    size_t count = sizeof(buf);

    KT_CHECK_EQ(katherine_udp_recv(&g_mock_quiet, buf, &count), KATHERINE_E_TIMEOUT);
}

// ------------------------------------------------------------------

// a) The monitor index goes in byte 0 and the chip index in byte 1, for
// every monitor the library exposes, against Table 11 written out above.
static void
test_a_the_monitor_index_and_chip_go_on_the_wire(void)
{
    for (unsigned monitor = 0; monitor < KATHERINE_TPX3_DAC_MONITOR_COUNT; ++monitor) {
        katherine_device_t device;
        kthread_t thread;
        float volts        = 0.0f;
        const uint8_t chip = (uint8_t) (monitor % GEN2_CHIPS);

        KT_REQUIRE(device_init(&device, HW_TYPE_GEN2, PORT_DEVICE, PORT_MOCK) == 0);

        memset(g_captured, 0xAA, sizeof(g_captured));
        KT_REQUIRE(kthread_start(&thread, mock_readout, NULL) == 0);

        const katherine_error_t res = katherine_tpx3_get_dac_monitor_voltage(&device, chip, (katherine_tpx3_dac_monitor_t) monitor, &volts);
        KT_CHECK_EQ(kthread_join(&thread), 0);

        printf("# monitor %u: res %d, answer %.4f\n", monitor, (int) res, (double) volts);
        KT_CHECK_EQ(res, 0);
        KT_CHECK_CLOSE(volts, MOCK_ANSWER_V);

        KT_CHECK_EQ(g_captured[0], TABLE_11[monitor]);
        KT_CHECK_EQ(g_captured[1], chip);
        KT_CHECK_EQ(g_captured[KATHERINE_CMD_OPCODE_BYTE], CMD_TYPE_INTERNAL_DAC_SCAN);

        // The sub-index byte a DAC *setting* uses stays empty here, the
        // monitor travelling as an index in byte 0 instead.
        KT_CHECK_EQ(g_captured[4], 0);

        katherine_udp_fini(&device.control_socket);
    }
}

// b) Every refusal happens before the send, so a readout that could not
// answer is never asked.
static void
test_b_bad_arguments_are_refused_without_asking(void)
{
    katherine_device_t device;
    float volts = 0.0f;

    // Neither enumerated nor declared: how many chips it addresses is unknown.
    KT_REQUIRE(device_init(&device, 0, PORT_DEVICE_QUIET, PORT_MOCK_QUIET) == 0);
    KT_CHECK_EQ(katherine_tpx3_get_dac_monitor_voltage(&device, 0, (katherine_tpx3_dac_monitor_t) 0, &volts),
        KATHERINE_E_STATE);
    expect_nothing_sent();
    katherine_udp_fini(&device.control_socket);

    // Past what a first-generation readout can address.
    KT_REQUIRE(device_init(&device, HW_TYPE_GEN1, PORT_DEVICE_QUIET, PORT_MOCK_QUIET) == 0);
    KT_CHECK_EQ(katherine_tpx3_get_dac_monitor_voltage(&device, GEN1_CHIPS,
                    (katherine_tpx3_dac_monitor_t) 0, &volts),
        KATHERINE_E_BAD_CHIP);
    expect_nothing_sent();

    // And a monitor outside the enumeration, in either direction, whose
    // index would be the chip's SenseOFF and so select nothing.
    KT_CHECK_EQ(katherine_tpx3_get_dac_monitor_voltage(&device, 0,
                    (katherine_tpx3_dac_monitor_t) KATHERINE_TPX3_DAC_MONITOR_COUNT, &volts),
        KATHERINE_E_INVAL);
    expect_nothing_sent();

    KT_CHECK_EQ(
        katherine_tpx3_get_dac_monitor_voltage(&device, 0, (katherine_tpx3_dac_monitor_t) -1, &volts),
        KATHERINE_E_INVAL);
    expect_nothing_sent();

    katherine_udp_fini(&device.control_socket);
}

// c) Against the emulated readout: a DAC's monitor answers what that DAC was
// set to, and the monitoring outputs answer the manual's nominal levels. This
// is the case that would catch an index being decoded back to the wrong
// monitor, the mock above having no opinion about which it was asked for.
static void
test_c_the_emulated_chip_answers_per_monitor(void)
{
    katherine_emu_t emu;
    uint8_t crd[KATHERINE_EMU_CRD_SIZE];
    size_t len;

    KT_REQUIRE(katherine_emu_init(&emu, NULL) == 0);

    for (unsigned monitor = 0; monitor < KATHERINE_TPX3_DAC_MONITOR_COUNT; ++monitor) {
        if (monitor < KATHERINE_TPX3_DAC_COUNT) {
            katherine_cmd_t set = katherine_cmd_create((uint8_t) CMD_TYPE_INTERNAL_DAC_SETTINGS);
            katherine_cmd_payload_set_subidx(&set, (uint8_t) monitor);
            katherine_cmd_payload_set_i64(&set, DAC_VALUE + (int) monitor);
            KT_REQUIRE(katherine_emu_cmd_in(&emu, set.b, sizeof(set.b)) == 0);
            KT_REQUIRE(katherine_emu_crd_out(&emu, crd, &len) == 0);
        }

        katherine_cmd_t scan = katherine_cmd_create((uint8_t) CMD_TYPE_INTERNAL_DAC_SCAN);
        scan.b[0]            = TABLE_11[monitor];
        KT_REQUIRE(katherine_emu_cmd_in(&emu, scan.b, sizeof(scan.b)) == 0);
        KT_CHECK_EQ(katherine_emu_crd_out(&emu, crd, &len), 0);
        KT_CHECK_EQ(crd[KATHERINE_CMD_OPCODE_BYTE], CMD_TYPE_INTERNAL_DAC_SCAN);

        if (monitor < KATHERINE_TPX3_DAC_COUNT) {
            KT_CHECK_CLOSE(crd_float(crd), EMU_VOLT_PER_STEP * (float) (DAC_VALUE + (int) monitor));
        } else {
            // Nominal levels of Tpx3 manual Table 11, all of them within the
            // converter's range and none of them zero.
            const float volts = crd_float(crd);
            printf("# monitor %u: %.4f\n", monitor, (double) volts);
            KT_CHECK(volts > 0.5f && volts < 1.25f);
        }
    }

    // The all-DAC scan walks the same monitors, so its answers are the same
    // sequence -- which is what makes one command's result comparable with
    // the other's.
    katherine_cmd_t all = katherine_cmd_create((uint8_t) CMD_TYPE_GET_ALL_DAC_SCAN);
    KT_REQUIRE(katherine_emu_cmd_in(&emu, all.b, sizeof(all.b)) == 0);
    for (unsigned monitor = 0; monitor < KATHERINE_TPX3_DAC_MONITOR_COUNT; ++monitor) {
        KT_CHECK_EQ(katherine_emu_crd_out(&emu, crd, &len), 0);
        if (monitor < KATHERINE_TPX3_DAC_COUNT) {
            KT_CHECK_CLOSE(crd_float(crd), EMU_VOLT_PER_STEP * (float) (DAC_VALUE + (int) monitor));
        }
    }
    KT_CHECK_EQ(katherine_emu_crd_out(&emu, crd, &len), KATHERINE_E_TIMEOUT);

    katherine_emu_fini(&emu);
}

int
main(void)
{
    const int res = mocks_init();
    if (res != 0) {
        printf("1..0 # SKIP cannot bind the loopback endpoints: %s\n", katherine_strerror(res));
        return 77;
    }

    KT_RUN(test_a_the_monitor_index_and_chip_go_on_the_wire);
    KT_RUN(test_b_bad_arguments_are_refused_without_asking);
    KT_RUN(test_c_the_emulated_chip_answers_per_monitor);

    mocks_fini();
    return kt_summary();
}
