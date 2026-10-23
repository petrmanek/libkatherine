/**
 * \file
 * \brief The chip a per-chip command is addressed to, and where it lands.
 *
 * Every command in KATHERINE_CHIP_ENVELOPE carries its chip index in a byte
 * the firmware picks per opcode, and three different bytes are in use. A
 * function that composes the command correctly but places the index in the
 * wrong byte is acknowledged exactly like a correct one, so nothing short of
 * reading the datagram that left tells the two apart.
 *
 * A mock readout on loopback answers, so the cases run with no hardware and
 * no emulator profile; what they check is the bytes that left.
 *
 * \author Petr Mánek
 * \date 22.10.26
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

#define PORT_DEVICE          (KPORTS_CHIP_ROUTING + 0)
#define PORT_MOCK            (KPORTS_CHIP_ROUTING + 1)
#define PORT_DEVICE_QUIET    (KPORTS_CHIP_ROUTING + 2)
#define PORT_MOCK_QUIET      (KPORTS_CHIP_ROUTING + 3)

#define DEVICE_TIMEOUT_MS    400
#define MOCK_TIMEOUT_MS      400
#define QUIET_TIMEOUT_MS     60

/* The fixture declares a readout reporting eight chips, so this one is one of
 * them and the one at eight is not. */
#define CHIP_PRESENT         3
#define CHIP_ABSENT          8

/* Where each opcode reads its chip index from, per KATHERINE_CHIP_ENVELOPE.
 * Spelled out here rather than taken from the table, so that a change to the
 * table has to be made twice and is noticed once. */
#define SENSOR_REG_CHIP_BYTE 5
#define HW_COMMAND_CHIP_BYTE 1

#define MAX_CAPTURED         4

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
        .hw_type    = 3,
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

// Runs one exchange against the mock and leaves what left in g_captured.
#define SERVE_ONE(CALL) \
    do { \
        kthread_t th; \
        g_to_serve = 1; \
        KT_REQUIRE(kthread_start(&th, mock_readout, NULL) == 0); \
        KT_CHECK_EQ((CALL), KATHERINE_E_OK); \
        KT_CHECK_EQ(kthread_join(&th), 0); \
        KT_REQUIRE(g_captured_count == 1); \
    } while (0)

// ------------------------------------------------------------------

/* The sensor-register write names its chip in byte 5, which is also where the
 * DAC write names its own: the two opcodes share a slot and neither shares it
 * with the hardware-command dispatch. */
static void
test_a_sensor_register_write_names_its_chip(void)
{
    katherine_device_t device;
    KT_REQUIRE(device_init(&device, PORT_DEVICE, PORT_MOCK) == KATHERINE_E_OK);

    SERVE_ONE(katherine_tpx3_set_sensor_register(
        &device, CHIP_PRESENT, KATHERINE_TPX3_REG_GENERAL_CONFIG, 0x58));

    KT_CHECK_EQ(g_captured[0][KATHERINE_CMD_OPCODE_BYTE], CMD_TYPE_SENSOR_REGISTER_SETTING);
    KT_CHECK_EQ(g_captured[0][SENSOR_REG_CHIP_BYTE], CHIP_PRESENT);

    katherine_transport_fini(&device.control_transport);
}

/* The three hardware commands take their chip index from byte 1, leaving byte
 * 0 to the sub-command that says which hardware command it is. */
static void
test_the_hardware_commands_name_their_chip(void)
{
    katherine_device_t device;
    KT_REQUIRE(device_init(&device, PORT_DEVICE, PORT_MOCK) == KATHERINE_E_OK);

    SERVE_ONE(katherine_tpx3_update_sensor_registers(&device, CHIP_PRESENT));
    KT_CHECK_EQ(g_captured[0][KATHERINE_CMD_OPCODE_BYTE], CMD_TYPE_HW_COMMAND_START);
    KT_CHECK_EQ(g_captured[0][HW_COMMAND_CHIP_BYTE], CHIP_PRESENT);

    SERVE_ONE(katherine_tpx3_output_block_config_update(&device, CHIP_PRESENT));
    KT_CHECK_EQ(g_captured[0][KATHERINE_CMD_OPCODE_BYTE], CMD_TYPE_HW_COMMAND_START);
    KT_CHECK_EQ(g_captured[0][HW_COMMAND_CHIP_BYTE], CHIP_PRESENT);

    SERVE_ONE(katherine_tpx3_timer_set(&device, CHIP_PRESENT));
    KT_CHECK_EQ(g_captured[0][KATHERINE_CMD_OPCODE_BYTE], CMD_TYPE_HW_COMMAND_START);
    KT_CHECK_EQ(g_captured[0][HW_COMMAND_CHIP_BYTE], CHIP_PRESENT);

    katherine_transport_fini(&device.control_transport);
}

/* The sub-command still reaches byte 0 with a chip index in byte 1: the two
 * arguments of a hardware command do not share a byte. */
static void
test_a_hardware_command_keeps_its_sub_command(void)
{
    katherine_device_t device;
    KT_REQUIRE(device_init(&device, PORT_DEVICE, PORT_MOCK) == KATHERINE_E_OK);

    SERVE_ONE(katherine_tpx3_timer_set(&device, CHIP_PRESENT));
    KT_CHECK_EQ(g_captured[0][0], CMD_START_TIMER_SET);

    SERVE_ONE(katherine_tpx3_output_block_config_update(&device, CHIP_PRESENT));
    KT_CHECK_EQ(g_captured[0][0], CMD_START_OUTPUT_BLOCK_CONFIG_UPDATE);

    katherine_transport_fini(&device.control_transport);
}

/* A chip this readout does not carry is refused before anything is sent, so
 * the write cannot land on a different chip than the caller named. */
static void
test_an_absent_chip_is_refused(void)
{
    katherine_device_t device;
    KT_REQUIRE(device_init(&device, PORT_DEVICE_QUIET, PORT_MOCK_QUIET) == KATHERINE_E_OK);

    KT_CHECK_EQ(katherine_tpx3_set_sensor_register(
                    &device, CHIP_ABSENT, KATHERINE_TPX3_REG_GENERAL_CONFIG, 0x58),
        KATHERINE_E_BAD_CHIP);
    KT_CHECK_EQ(katherine_tpx3_update_sensor_registers(&device, CHIP_ABSENT), KATHERINE_E_BAD_CHIP);
    KT_CHECK_EQ(
        katherine_tpx3_output_block_config_update(&device, CHIP_ABSENT), KATHERINE_E_BAD_CHIP);
    KT_CHECK_EQ(katherine_tpx3_timer_set(&device, CHIP_ABSENT), KATHERINE_E_BAD_CHIP);

    uint8_t buf[KATHERINE_CMD_CRD_SIZE];
    size_t count = sizeof(buf);
    KT_CHECK_EQ(katherine_udp_recv(&g_mock_quiet, buf, &count), KATHERINE_E_TIMEOUT);

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

    KT_RUN(test_a_sensor_register_write_names_its_chip);
    KT_RUN(test_the_hardware_commands_name_their_chip);
    KT_RUN(test_a_hardware_command_keeps_its_sub_command);
    KT_RUN(test_an_absent_chip_is_refused);

    katherine_udp_fini(&g_mock_quiet);
    katherine_udp_fini(&g_mock);

    return kt_summary();
}
