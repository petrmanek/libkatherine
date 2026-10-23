/**
 * \file
 * \brief Asking a readout something and discarding the answer.
 *
 * Several of these accessors already took NULL and the rest dereferenced it,
 * with nothing in the signatures to tell the two groups apart. A caller that
 * wants the command issued and not the value -- a liveness check, a flush of
 * a stale response, a command whose effect is the point -- had to know which
 * group it was in.
 *
 * Each case answers from a mock readout on loopback and passes NULL, so what
 * is checked is that the call reports success without writing anywhere. Under
 * the address sanitizer leg this is also where a reinstated dereference shows
 * up as a fault rather than as a silent write through a null pointer.
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

// katherine/katherine.h must precede kthread.h on Windows; see
// test_cmd_transact.c, which documents the include order.
#include <katherine/katherine.h>

#include "transport/udp.h"

#include "protocol/cmd_interface.h"
#include "kthread.h"
#include "kports.h"
#include "ktest.h"

#define PORT_DEVICE       (KPORTS_STATUS_NULL + 0)
#define PORT_MOCK         (KPORTS_STATUS_NULL + 1)

#define DEVICE_TIMEOUT_MS 400
#define MOCK_TIMEOUT_MS   400

// Gen2, so that the bias accessors are available at all.
#define HW_TYPE_GEN2      0x03

// A chip identifier whose low nibble is nonzero, which is what
// katherine_get_chip_id() requires of a response it will accept.
#define CHIP_ID_NONZERO   0x00114251

static katherine_udp_t g_mock;
static uint32_t g_reply_word;

// Answers one command with a fixed payload under the opcode that was asked,
// which is what the library's response correlation looks for.
static void *
mock_readout(void *arg)
{
    uint8_t request[KATHERINE_CMD_CRD_SIZE];
    uint8_t crd[KATHERINE_CMD_CRD_SIZE] = {0};

    (void) arg;

    if (katherine_udp_recv_exact(&g_mock, request, sizeof(request)) != 0) return NULL;

    memcpy(crd, &g_reply_word, sizeof(g_reply_word));
    crd[KATHERINE_CMD_OPCODE_BYTE] = request[KATHERINE_CMD_OPCODE_BYTE];
    (void) katherine_udp_send_exact(&g_mock, crd, sizeof(crd));

    return NULL;
}

static katherine_error_t
device_init(katherine_device_t *device)
{
    memset(device, 0, sizeof(*device));

    const katherine_device_info_t info = {
        .hw_type    = HW_TYPE_GEN2,
        .chip_count = 1,
    };
    if (katherine_device_declare(device, &info) != KATHERINE_E_OK) return KATHERINE_E_INVAL;

    const katherine_udp_config_t config = {
        .local_address  = "127.0.0.1",
        .local_port     = PORT_DEVICE,
        .remote_address = "127.0.0.1",
        .remote_port    = PORT_MOCK,
        .timeout_ms     = DEVICE_TIMEOUT_MS,
    };

    return katherine_transport_init_udp(&device->control_transport, &config);
}

// Runs one accessor against one mock answer. The call is made on this thread
// so that a dereference of NULL faults here, where the case can be named.
#define CHECK_DISCARDS(reply, call) \
    do { \
        katherine_device_t device; \
        KT_REQUIRE(device_init(&device) == KATHERINE_E_OK); \
        kthread_t thread; \
        g_reply_word = (reply); \
        KT_REQUIRE(kthread_start(&thread, mock_readout, NULL) == 0); \
        KT_CHECK_EQ((call), KATHERINE_E_OK); \
        KT_CHECK_EQ(kthread_join(&thread), 0); \
        katherine_transport_fini(&device.control_transport); \
        katherine_transport_fini(&device.control_transport); \
    } while (0)

// ------------------------------------------------------------------

static void
test_readout_status_can_be_discarded(void)
{
    CHECK_DISCARDS(0x12345678u, katherine_get_readout_status(&device, NULL));
}

static void
test_comm_status_can_be_discarded(void)
{
    CHECK_DISCARDS(0x00010203u, katherine_get_comm_status(&device, NULL));
}

static void
test_a_chip_identifier_can_be_discarded(void)
{
    CHECK_DISCARDS(CHIP_ID_NONZERO, katherine_get_chip_id(&device, 0, NULL));
}

static void
test_a_readout_temperature_can_be_discarded(void)
{
    CHECK_DISCARDS(0x41C80000u, katherine_get_readout_temperature(&device, NULL));
}

static void
test_a_sensor_temperature_can_be_discarded(void)
{
    CHECK_DISCARDS(0x41C80000u, katherine_get_sensor_temperature(&device, 0, NULL));
}

static void
test_an_adc_voltage_can_be_discarded(void)
{
    CHECK_DISCARDS(0x3F800000u, katherine_get_adc_voltage(&device, 0, NULL));
}

/* These two already took NULL before the rest did; they are here so that the
 * whole group is covered by one case list rather than by two conventions. */
static void
test_a_bias_voltage_can_be_discarded(void)
{
    CHECK_DISCARDS(0x43160000u, katherine_get_bias(&device, 0, NULL));
}

static void
test_a_bias_leakage_can_be_discarded(void)
{
    CHECK_DISCARDS(0x40000000u, katherine_get_bias_leakage(&device, 0, NULL));
}

// ------------------------------------------------------------------

int
main(void)
{
    if (katherine_udp_init_bound(
            &g_mock, "127.0.0.1", PORT_MOCK, "127.0.0.1", PORT_DEVICE, MOCK_TIMEOUT_MS)
        != 0)
        return 1;

    KT_RUN(test_readout_status_can_be_discarded);
    KT_RUN(test_comm_status_can_be_discarded);
    KT_RUN(test_a_chip_identifier_can_be_discarded);
    KT_RUN(test_a_readout_temperature_can_be_discarded);
    KT_RUN(test_a_sensor_temperature_can_be_discarded);
    KT_RUN(test_an_adc_voltage_can_be_discarded);
    KT_RUN(test_a_bias_voltage_can_be_discarded);
    KT_RUN(test_a_bias_leakage_can_be_discarded);

    katherine_udp_fini(&g_mock);

    return kt_summary();
}
