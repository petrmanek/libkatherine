/**
 * \file
 * \brief Setting one DAC without resending the other seventeen.
 *
 * The chip takes a DAC when the update command latches the readout's shadow of
 * its registers, so writing one is two exchanges rather than one. What is
 * asserted here is the pair, the sub-index that selects the DAC, and the width
 * of the value: Vthreshold_fine and VTP_fine reach 511, so a working threshold
 * of 388 is lost entirely if the value is ever narrowed to a byte.
 *
 * A mock readout on loopback answers, so the cases run with no hardware and no
 * emulator profile; what they check is the bytes that left.
 *
 * \author Petr Mánek
 * \date 15.10.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <string.h>

#include <katherine/katherine.h>

#include "transport/udp.h"

#include "kports.h"
#include "kthread.h"
#include "protocol/cmd_interface.h"

#include "ktest.h"

#define PORT_DEVICE         (KPORTS_SET_DAC + 0)
#define PORT_MOCK           (KPORTS_SET_DAC + 1)
#define PORT_DEVICE_QUIET   (KPORTS_SET_DAC + 2)
#define PORT_MOCK_QUIET     (KPORTS_SET_DAC + 3)

#define DEVICE_TIMEOUT_MS   400
#define MOCK_TIMEOUT_MS     400
#define QUIET_TIMEOUT_MS    60

/* Working values measured on I8-W0036: coarse 7, fine 388. The fine one is
 * the case that matters, being past what a byte holds. */
#define THRESHOLD_FINE      388

/* The fixture declares a readout reporting eight chips, so this one is one of
 * them and the one at eight is not. The gate asks what the readout reports,
 * not what its hardware type could hold. */
#define CHIP_PRESENT        2
#define CHIP_ABSENT         8

/* Where each opcode reads its chip index from, per KATHERINE_CHIP_ENVELOPE:
 * the DAC write from byte 5, the hardware command that latches from byte 1. */
#define DAC_WRITE_CHIP_BYTE 5
#define LATCH_CHIP_BYTE     1

#define MAX_CAPTURED        4

static katherine_udp_t g_mock;
static katherine_udp_t g_mock_quiet;
static uint8_t g_captured[MAX_CAPTURED][KATHERINE_CMD_CRD_SIZE];
static int g_captured_count;
static int g_to_serve;

// Answers g_to_serve commands, echoing each opcode back so the library's own
// correlation accepts the acknowledgement, and keeping every datagram.
static void *
mock_readout(void *arg)
{
    (void) arg;

    g_captured_count = 0;

    for (int i = 0; i < g_to_serve && i < MAX_CAPTURED; ++i) {
        uint8_t crd[KATHERINE_CMD_CRD_SIZE] = {0};

        if (katherine_udp_recv_exact(&g_mock, g_captured[i], KATHERINE_CMD_CRD_SIZE) != 0) break;
        g_captured_count += 1;

        crd[KATHERINE_CMD_OPCODE_BYTE] = g_captured[i][KATHERINE_CMD_OPCODE_BYTE];
        if (katherine_udp_send_exact(&g_mock, crd, sizeof(crd)) != 0) break;
    }

    return NULL;
}

static katherine_error_t
device_init(katherine_device_t *device, uint16_t local_port, uint16_t remote_port)
{
    memset(device, 0, sizeof(*device));

    const katherine_device_info_t info = {
        .status     = {.hw_type = 3},
        .chip_count = 8,
    };
    if (katherine_device_declare(device, &info) != KATHERINE_E_OK) return KATHERINE_E_INVAL;

    const katherine_udp_config_t config = {
        .local_address  = "127.0.0.1",
        .local_port     = local_port,
        .remote_address = "127.0.0.1",
        .remote_port    = remote_port,
        .timeout_ms     = DEVICE_TIMEOUT_MS,
    };

    return katherine_transport_init_udp(&device->control_transport, &config);
}

static uint32_t
captured_payload(int i)
{
    return (uint32_t) g_captured[i][0] | ((uint32_t) g_captured[i][1] << 8)
        | ((uint32_t) g_captured[i][2] << 16) | ((uint32_t) g_captured[i][3] << 24);
}

// ------------------------------------------------------------------

/* The write names its DAC in the sub-index and carries the value whole, and
 * the update follows it so the chip actually takes it. A write without the
 * update would be acknowledged and have no effect on the sensor. */
static void
test_one_dac_is_written_then_latched(void)
{
    katherine_device_t device;
    KT_REQUIRE(device_init(&device, PORT_DEVICE, PORT_MOCK) == KATHERINE_E_OK);

    kthread_t th;
    g_to_serve = 2;
    KT_REQUIRE(kthread_start(&th, mock_readout, NULL) == 0);

    KT_CHECK_EQ(
        katherine_tpx3_set_dac(&device, 0, KATHERINE_TPX3_DAC_VTHRESHOLD_FINE, THRESHOLD_FINE),
        KATHERINE_E_OK);

    KT_CHECK_EQ(kthread_join(&th), 0);

    KT_REQUIRE(g_captured_count == 2);
    KT_CHECK_EQ(g_captured[0][KATHERINE_CMD_OPCODE_BYTE], CMD_TYPE_INTERNAL_DAC_SETTINGS);
    KT_CHECK_EQ(g_captured[0][4], (uint8_t) KATHERINE_TPX3_DAC_VTHRESHOLD_FINE);
    KT_CHECK_EQ(g_captured[1][KATHERINE_CMD_OPCODE_BYTE], CMD_TYPE_HW_COMMAND_START);

    katherine_transport_fini(&device.control_transport);
    katherine_transport_fini(&device.control_transport);
}

/* The value reaches the wire whole. A threshold of 388 narrowed to a byte
 * would arrive as 132, which is a different and perfectly plausible
 * threshold -- so this fails loudly only if the payload is compared. */
static void
test_a_value_past_a_byte_survives(void)
{
    katherine_device_t device;
    KT_REQUIRE(device_init(&device, PORT_DEVICE, PORT_MOCK) == KATHERINE_E_OK);

    kthread_t th;
    g_to_serve = 2;
    KT_REQUIRE(kthread_start(&th, mock_readout, NULL) == 0);

    KT_CHECK_EQ(
        katherine_tpx3_set_dac(&device, 0, KATHERINE_TPX3_DAC_VTHRESHOLD_FINE, THRESHOLD_FINE),
        KATHERINE_E_OK);

    KT_CHECK_EQ(kthread_join(&th), 0);

    KT_REQUIRE(g_captured_count >= 1);
    KT_CHECK_EQ(captured_payload(0), (uint32_t) THRESHOLD_FINE);
    KT_CHECK(captured_payload(0) != (THRESHOLD_FINE & 0xFFU));

    katherine_transport_fini(&device.control_transport);
    katherine_transport_fini(&device.control_transport);
}

/* The last DAC is addressable and the one past it is not, which is the pair
 * that fails if the bound is written with the wrong comparison. */
static void
test_the_bounds_of_the_enumeration(void)
{
    katherine_device_t device;
    KT_REQUIRE(device_init(&device, PORT_DEVICE, PORT_MOCK) == KATHERINE_E_OK);

    kthread_t th;
    g_to_serve = 2;
    KT_REQUIRE(kthread_start(&th, mock_readout, NULL) == 0);

    KT_CHECK_EQ(katherine_tpx3_set_dac(&device, 0, KATHERINE_TPX3_DAC_PLL_VCNTRL, 1),
        KATHERINE_E_OK);

    KT_CHECK_EQ(kthread_join(&th), 0);
    KT_CHECK_EQ(g_captured[0][4], (uint8_t) (KATHERINE_TPX3_DAC_COUNT - 1));

    KT_CHECK_EQ(
        katherine_tpx3_set_dac(&device, 0, (katherine_tpx3_dac_t) KATHERINE_TPX3_DAC_COUNT, 1),
        KATHERINE_E_INVAL);

    katherine_transport_fini(&device.control_transport);
    katherine_transport_fini(&device.control_transport);
}

/* A refused DAC is refused before anything is sent: a caller that saw
 * KATHERINE_E_INVAL has not changed the chip. */
static void
test_a_refused_dac_sends_nothing(void)
{
    katherine_device_t device;
    KT_REQUIRE(device_init(&device, PORT_DEVICE_QUIET, PORT_MOCK_QUIET) == KATHERINE_E_OK);

    KT_CHECK_EQ(
        katherine_tpx3_set_dac(&device, 0, (katherine_tpx3_dac_t) KATHERINE_TPX3_DAC_COUNT, 1),
        KATHERINE_E_INVAL);

    uint8_t buf[KATHERINE_CMD_CRD_SIZE];
    size_t count = sizeof(buf);
    KT_CHECK_EQ(katherine_udp_recv(&g_mock_quiet, buf, &count), KATHERINE_E_TIMEOUT);

    katherine_transport_fini(&device.control_transport);
    katherine_transport_fini(&device.control_transport);
}

/* The chip index reaches the wire, in the byte each opcode reads it from.
 * Until this landed every DAC write went to chip 0 whatever the caller asked,
 * because the encoder never filled the field the firmware reads. */
static void
test_the_chip_index_reaches_both_commands(void)
{
    katherine_device_t device;
    KT_REQUIRE(device_init(&device, PORT_DEVICE, PORT_MOCK) == KATHERINE_E_OK);

    kthread_t th;
    g_to_serve = 2;
    KT_REQUIRE(kthread_start(&th, mock_readout, NULL) == 0);

    KT_CHECK_EQ(katherine_tpx3_set_dac(
                    &device, CHIP_PRESENT, KATHERINE_TPX3_DAC_VTHRESHOLD_FINE, THRESHOLD_FINE),
        KATHERINE_E_OK);

    KT_CHECK_EQ(kthread_join(&th), 0);

    KT_REQUIRE(g_captured_count == 2);
    KT_CHECK_EQ(g_captured[0][DAC_WRITE_CHIP_BYTE], CHIP_PRESENT);
    KT_CHECK_EQ(g_captured[1][LATCH_CHIP_BYTE], CHIP_PRESENT);

    katherine_transport_fini(&device.control_transport);
    katherine_transport_fini(&device.control_transport);
}

/* A chip this readout does not carry is refused before anything is sent, so
 * the write cannot land on a different chip than the caller named. */
static void
test_an_absent_chip_is_refused(void)
{
    katherine_device_t device;
    KT_REQUIRE(device_init(&device, PORT_DEVICE_QUIET, PORT_MOCK_QUIET) == KATHERINE_E_OK);

    KT_CHECK_EQ(katherine_tpx3_set_dac(
                    &device, CHIP_ABSENT, KATHERINE_TPX3_DAC_VTHRESHOLD_FINE, THRESHOLD_FINE),
        KATHERINE_E_BAD_CHIP);

    katherine_tpx3_dacs_t dacs;
    memset(&dacs, 0, sizeof(dacs));
    KT_CHECK_EQ(katherine_tpx3_set_dacs(&device, CHIP_ABSENT, &dacs), KATHERINE_E_BAD_CHIP);

    uint8_t buf[KATHERINE_CMD_CRD_SIZE];
    size_t count = sizeof(buf);
    KT_CHECK_EQ(katherine_udp_recv(&g_mock_quiet, buf, &count), KATHERINE_E_TIMEOUT);

    katherine_transport_fini(&device.control_transport);
    katherine_transport_fini(&device.control_transport);
}

/* A device that was never told what it is refuses every chip-addressed call,
 * chip 0 included. Nothing is special about the first chip: a readout reports
 * how many it carries, and before it has been asked -- or declared -- the
 * honest answer is none.
 *
 * This case asserted the opposite until 2026-10-18, when the contract was
 * settled the other way: a device opened with KATHERINE_DEVICE_DONT_ENUMERATE
 * is expected to be given katherine_device_declare() shortly afterwards, and
 * nothing is expected to work before that. */
static void
test_an_undeclared_device_refuses_even_chip_zero(void)
{
    katherine_device_t device;
    memset(&device, 0, sizeof(device));
    const katherine_udp_config_t config = {
        .local_address  = "127.0.0.1",
        .local_port     = PORT_DEVICE,
        .remote_address = "127.0.0.1",
        .remote_port    = PORT_MOCK,
        .timeout_ms     = DEVICE_TIMEOUT_MS,
    };
    KT_REQUIRE(katherine_transport_init_udp(&device.control_transport, &config) == KATHERINE_E_OK);
    KT_REQUIRE(device.info.chip_count == 0);

    /* Refused before the link is touched, so no mock is needed and nothing
     * reaches the wire. */
    KT_CHECK_EQ(katherine_tpx3_set_dac(&device, 0, KATHERINE_TPX3_DAC_VTHRESHOLD_FINE, THRESHOLD_FINE),
        KATHERINE_E_BAD_CHIP);

    katherine_tpx3_dacs_t dacs;
    memset(&dacs, 0, sizeof(dacs));
    KT_CHECK_EQ(katherine_tpx3_set_dacs(&device, 0, &dacs), KATHERINE_E_BAD_CHIP);

    katherine_transport_fini(&device.control_transport);
    katherine_transport_fini(&device.control_transport);
}

// ------------------------------------------------------------------

int
main(void)
{
    if (katherine_udp_init_bound(
            &g_mock, "127.0.0.1", PORT_MOCK, "127.0.0.1", PORT_DEVICE, MOCK_TIMEOUT_MS)
        != 0)
        return 1;

    if (katherine_udp_init_bound(&g_mock_quiet, "127.0.0.1", PORT_MOCK_QUIET, "127.0.0.1",
            PORT_DEVICE_QUIET, QUIET_TIMEOUT_MS)
        != 0) {
        katherine_udp_fini(&g_mock);
        return 1;
    }

    KT_RUN(test_one_dac_is_written_then_latched);
    KT_RUN(test_a_value_past_a_byte_survives);
    KT_RUN(test_the_bounds_of_the_enumeration);
    KT_RUN(test_a_refused_dac_sends_nothing);
    KT_RUN(test_the_chip_index_reaches_both_commands);
    KT_RUN(test_an_absent_chip_is_refused);
    KT_RUN(test_an_undeclared_device_refuses_even_chip_zero);

    katherine_udp_fini(&g_mock_quiet);
    katherine_udp_fini(&g_mock);

    return kt_summary();
}
