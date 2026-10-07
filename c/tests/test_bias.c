/**
 * \file
 * \brief What the readout answers when asked about a bias supply.
 *
 * Two operation codes, neither of which returns what its name suggests.
 * `0x0C` answers a voltage the readout has read back through its own
 * converter, so it is the setpoint quantized to about 0.505 V rather than the
 * setpoint itself; `0x30`, named after a current, answers the voltage across
 * a sense resistor and only on the second generation, one first-generation
 * firmware having spent the code on something else entirely.
 *
 * The first cases drive the emulated readout in memory, with no sockets, so
 * that the model of the peer is what is asserted against. The rest put a mock
 * readout on loopback and check what the accessors put on the wire: the index
 * of the supply goes in two bytes, the manual specifying one and the firmware
 * reading the other, and a readout that cannot answer the sense reading at
 * all is never asked.
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
#include <stdio.h>
#include <stdint.h>
#include <string.h>

// katherine/katherine.h must precede kthread.h on Windows; see
// test_cmd_transact.c, which documents the include order.
#include <katherine/katherine.h>
#include <katherine/emulator.h>

#include "protocol/cmd_builder.h"
#include "protocol/cmd_interface.h"
#include "kthread.h"
#include "ktest.h"

// Hardware types of the two generations, as c/src/device/hw_info.c maps them.
#define HW_TYPE_GEN1      0x01
#define HW_TYPE_GEN2      0x03

// One step of the readout's bias converter, 2.5 V over 12 bits through the
// divider and its compensation. Read-backs land on a multiple of it.
#define BIAS_STEP_V       0.50473f

// Bias setpoint the cases work at: positive, as a hole-collecting sensor
// needs, and far enough from zero that a quantization error cannot be
// confused with the sign.
#define BIAS_SET_V        150.0f

// Converter steps between the read-backs of that setpoint and of zero, as
// the lab readout answered for the same change.
#define BIAS_SET_V_STEPS  297

// Loopback endpoints of the mock readout, high and uncommon, distinct from
// every other fixed set in this tree (test_cmd_transact.c's 4261x,
// test_udp_pinning.c's 42555-42557, test_cmd_encoders.c's 42600/42601, the
// ksim daemon's 1555/1556).
#define PORT_DEVICE       42620
#define PORT_MOCK         42621
#define PORT_DEVICE_QUIET 42622
#define PORT_MOCK_QUIET   42623

// Headroom against scheduling jitter on the case that answers: the command
// reaches the mock within microseconds of the thread starting.
#define MOCK_TIMEOUT_MS   5000

// Spent in full by the cases that assert nothing was sent, so kept short.
#define QUIET_TIMEOUT_MS  100

// Receive timeout of the device under test. Only the refusing cases could
// reach it, and they are not expected to.
#define DEVICE_TIMEOUT_MS 2000

// Index of the supply a case asks about. The indices run 0 to one less than
// the count the readout provides, so the first generation has only 0 while the
// second also has 1. The second-generation cases use 1: zero is what the
// envelope builder leaves in a byte nobody wrote, and an assertion against it
// could not tell an index that was placed from one that was not.
#define BIAS_ID_GEN1      0
#define BIAS_ID_GEN2      1

// What the mock answers with, distinctive enough to tell from a misread byte.
#define MOCK_ANSWER_V     1.875f

// The conversion katherine_get_bias_leakage() puts that answer through: a
// sense voltage read against a zero-current reference, across a sense
// resistor. Neither constant is exported, so c/src/device/status.c -- which
// carries the TODOs on measuring both -- is the only statement of them.
#define SENSE_ZERO_V      2.024f
#define SENSE_OHMS        6000.0f
#define SENSE_TO_UA(v)    (1e6f * (SENSE_ZERO_V - (v)) / SENSE_OHMS)

static float
crd_float(const uint8_t *crd)
{
    float value;
    memcpy(&value, crd, sizeof(value));
    return value;
}

// Hands the emulator one command datagram and returns its single response,
// or KATHERINE_E_TIMEOUT if it answered nothing.
static katherine_error_t
exchange(katherine_emu_t *emu, katherine_cmd_t cmd, uint8_t *crd)
{
    size_t len = 0;

    const katherine_error_t res = katherine_emu_cmd_in(emu, cmd.b, sizeof(cmd.b));
    if (res != KATHERINE_E_OK) return res;

    return katherine_emu_crd_out(emu, crd, &len);
}

static void
emu_init(katherine_emu_t *emu, uint8_t hw_type)
{
    katherine_emu_profile_t profile;

    katherine_emu_profile_defaults(&profile);
    profile.hw_type = hw_type;
    KT_REQUIRE(katherine_emu_init(emu, &profile) == 0);
}

static void
set_bias(katherine_emu_t *emu, float volts)
{
    uint8_t crd[KATHERINE_EMU_CRD_SIZE];

    katherine_cmd_t cmd = katherine_cmd_create((uint8_t) CMD_TYPE_BIAS_SETTINGS);
    katherine_cmd_payload_set_f32(&cmd, volts);
    KT_REQUIRE(exchange(emu, cmd, crd) == 0);
}

// ------------------------------------------------------------------
// A mock readout on loopback, and a device declared rather than enumerated so
// that its generation is chosen by the case instead of asked for.

static katherine_udp_t g_mock;
static katherine_udp_t g_mock_quiet;
static uint8_t g_captured[KATHERINE_CMD_CRD_SIZE];
static uint8_t g_reply_opcode;

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

// Receives one command, records it whole, and answers it with a float under
// the identifier the case chose.
static void *
mock_readout(void *arg)
{
    uint8_t crd[KATHERINE_CMD_CRD_SIZE] = {0};
    const float value                   = MOCK_ANSWER_V;

    (void) arg;

    if (katherine_udp_recv_exact(&g_mock, g_captured, sizeof(g_captured)) != 0) return NULL;

    memcpy(crd, &value, sizeof(value));
    crd[KATHERINE_CMD_OPCODE_BYTE] = g_reply_opcode;
    (void) katherine_udp_send_exact(&g_mock, crd, sizeof(crd));

    return NULL;
}

// A device aimed at one of the mocks. A zero hardware type leaves it
// undeclared, so that its generation is unknown -- itself a case below.
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

// Requires the mock to have been sent nothing, which costs its whole receive
// timeout and so is only asked of the short-timeout one.
static void
expect_nothing_sent(void)
{
    uint8_t buf[KATHERINE_CMD_CRD_SIZE];
    size_t count = sizeof(buf);

    KT_CHECK_EQ(katherine_udp_recv(&g_mock_quiet, buf, &count), KATHERINE_E_TIMEOUT);
}

// ------------------------------------------------------------------

// a) The read-back follows the setpoint, but through the converter: it lands
// on a multiple of the step and so is close to the setpoint without being
// equal to it. A model that echoed the setpoint would pass the first check
// and fail the second.
static void
test_a_read_back_is_the_setpoint_quantized(void)
{
    katherine_emu_t emu;
    uint8_t crd[KATHERINE_EMU_CRD_SIZE];

    emu_init(&emu, HW_TYPE_GEN2);
    set_bias(&emu, BIAS_SET_V);

    katherine_cmd_t cmd = katherine_cmd_create((uint8_t) CMD_TYPE_GET_BIAS_VOLTAGE);
    KT_CHECK_EQ(exchange(&emu, cmd, crd), 0);
    KT_CHECK_EQ(crd[KATHERINE_CMD_OPCODE_BYTE], CMD_TYPE_GET_BIAS_VOLTAGE);

    const float volts = crd_float(crd);
    KT_CHECK(fabsf(volts - BIAS_SET_V) < BIAS_STEP_V);
    KT_CHECK(volts != BIAS_SET_V);

    // A setpoint of zero does not read back as zero either, the converter's
    // own midpoint being what the scale is referred to. The two read-backs
    // stand a whole number of steps apart, and for this setpoint that number
    // is the one the lab readout showed for the same change.
    set_bias(&emu, 0.0f);
    KT_CHECK_EQ(exchange(&emu, cmd, crd), 0);
    KT_CHECK_NEAR((volts - crd_float(crd)) / BIAS_STEP_V, BIAS_SET_V_STEPS, 0.01);

    katherine_emu_fini(&emu);
}

// b) Both generations answer the voltage read-back, so this one carries no
// generation gate to get wrong.
static void
test_b_read_back_answers_on_both_generations(void)
{
    katherine_emu_t emu;
    uint8_t crd[KATHERINE_EMU_CRD_SIZE];

    emu_init(&emu, HW_TYPE_GEN1);

    katherine_cmd_t cmd = katherine_cmd_create((uint8_t) CMD_TYPE_GET_BIAS_VOLTAGE);
    KT_CHECK_EQ(exchange(&emu, cmd, crd), 0);
    KT_CHECK_EQ(crd[KATHERINE_CMD_OPCODE_BYTE], CMD_TYPE_GET_BIAS_VOLTAGE);

    katherine_emu_fini(&emu);
}

// c) The sense reading is a voltage near the zero-current reference and does
// not move with the bias: a supply driving nothing draws nothing, whatever
// the setpoint. It is also not zero, which is what the model used to answer.
static void
test_c_sense_is_a_voltage_that_the_bias_does_not_move(void)
{
    katherine_emu_t emu;
    uint8_t crd[KATHERINE_EMU_CRD_SIZE];

    emu_init(&emu, HW_TYPE_GEN2);

    katherine_cmd_t cmd = katherine_cmd_create((uint8_t) CMD_TYPE_GET_BIAS_CURRENT);
    KT_CHECK_EQ(exchange(&emu, cmd, crd), 0);
    KT_CHECK_EQ(crd[KATHERINE_CMD_OPCODE_BYTE], CMD_TYPE_GET_BIAS_CURRENT);

    const float at_rest = crd_float(crd);
    KT_CHECK(at_rest > 1.0f && at_rest < 2.5f);

    set_bias(&emu, BIAS_SET_V);
    KT_CHECK_EQ(exchange(&emu, cmd, crd), 0);
    KT_CHECK_CLOSE(crd_float(crd), at_rest);

    katherine_emu_fini(&emu);
}

// d) The first generation answers it not at all, so a caller that sent it
// anyway would wait out its receive timeout. Silence, not an error response:
// the firmware's dispatcher has no default branch.
static void
test_d_sense_is_unanswered_below_the_second_generation(void)
{
    katherine_emu_t emu;
    uint8_t crd[KATHERINE_EMU_CRD_SIZE];

    emu_init(&emu, HW_TYPE_GEN1);

    katherine_cmd_t cmd = katherine_cmd_create((uint8_t) CMD_TYPE_GET_BIAS_CURRENT);
    KT_CHECK_EQ(exchange(&emu, cmd, crd), KATHERINE_E_TIMEOUT);

    // Recognized all the same: the code is spent on another command there,
    // not missing from the dispatcher.
    KT_CHECK_EQ(katherine_emu_unknown_cmd_count(&emu), 0);

    katherine_emu_fini(&emu);
}

// e) Both accessors carry the index in byte 0 and in byte 4. The manual
// specifies byte 4, the second-generation firmware reads byte 0, and the
// reference implementation sets both -- so both is what goes on the wire,
// and dropping either would still pass against a peer reading the other.
static void
test_e_the_supply_index_goes_in_both_bytes(void)
{
    // Automatic rather than static: the two accessors are dllimport on
    // Windows, whose addresses a static initializer cannot hold.
    const struct {
        const char *what;
        uint8_t opcode;
        uint8_t hw_type;
        katherine_error_t (*call)(katherine_device_t *, unsigned char, float *);
        uint8_t bias_id;

        // What the accessor is to make of MOCK_ANSWER_V: the read-back hands
        // the float back as it came, the leakage reading converts it.
        float expected;
    } cases[] = {
        {"voltage", (uint8_t) CMD_TYPE_GET_BIAS_VOLTAGE, HW_TYPE_GEN1, katherine_get_bias,
            BIAS_ID_GEN1, MOCK_ANSWER_V},
        {"leakage", (uint8_t) CMD_TYPE_GET_BIAS_CURRENT, HW_TYPE_GEN2, katherine_get_bias_leakage,
            BIAS_ID_GEN2, SENSE_TO_UA(MOCK_ANSWER_V)},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        katherine_device_t device;
        kthread_t thread;
        float volts = 0.0f;

        KT_REQUIRE(device_init(&device, cases[i].hw_type, PORT_DEVICE, PORT_MOCK) == 0);

        g_reply_opcode = cases[i].opcode;
        memset(g_captured, 0xAA, sizeof(g_captured));
        KT_REQUIRE(kthread_start(&thread, mock_readout, NULL) == 0);

        const katherine_error_t res = cases[i].call(&device, cases[i].bias_id, &volts);
        KT_CHECK_EQ(kthread_join(&thread), 0);

        printf("# %s: res %d, answer %.4f\n", cases[i].what, (int) res, (double) volts);
        KT_CHECK_EQ(res, 0);
        KT_CHECK_CLOSE(volts, cases[i].expected);

        KT_CHECK_EQ(g_captured[0], cases[i].bias_id);
        KT_CHECK_EQ(g_captured[4], cases[i].bias_id);
        KT_CHECK_EQ(g_captured[KATHERINE_CMD_OPCODE_BYTE], cases[i].opcode);

        // And nothing else: the index is the whole payload.
        KT_CHECK_EQ(g_captured[1], 0);
        KT_CHECK_EQ(g_captured[2], 0);
        KT_CHECK_EQ(g_captured[3], 0);
        KT_CHECK_EQ(g_captured[5], 0);
        KT_CHECK_EQ(g_captured[7], 0);

        katherine_udp_fini(&device.control_socket);
    }
}

// f) A first-generation readout is refused rather than asked, one Mini build
// spending the same code on a communication-setup command that reads the very
// byte the index would occupy. Refused before the send, not after a timeout.
static void
test_f_sense_refuses_the_first_generation_without_asking(void)
{
    katherine_device_t device;
    float volts = 0.0f;

    KT_REQUIRE(device_init(&device, HW_TYPE_GEN1, PORT_DEVICE_QUIET, PORT_MOCK_QUIET) == 0);

    KT_CHECK_EQ(katherine_get_bias_leakage(&device, BIAS_ID_GEN1, &volts), KATHERINE_E_UNSUPPORTED);
    expect_nothing_sent();

    katherine_udp_fini(&device.control_socket);
}

// g) A readout neither enumerated nor declared is refused for a different
// reason: its generation is unknown, so whether the command exists there is
// unknown too.
static void
test_g_sense_refuses_an_undeclared_readout(void)
{
    katherine_device_t device;
    float volts = 0.0f;

    KT_REQUIRE(device_init(&device, 0, PORT_DEVICE_QUIET, PORT_MOCK_QUIET) == 0);

    KT_CHECK_EQ(katherine_get_bias_leakage(&device, BIAS_ID_GEN1, &volts), KATHERINE_E_STATE);
    expect_nothing_sent();

    katherine_udp_fini(&device.control_socket);
}

int
main(void)
{
    const int res = mocks_init();
    if (res != 0) {
        printf("1..0 # SKIP cannot bind the loopback endpoints: %s\n", katherine_strerror(res));
        return 77;
    }

    KT_RUN(test_a_read_back_is_the_setpoint_quantized);
    KT_RUN(test_b_read_back_answers_on_both_generations);
    KT_RUN(test_c_sense_is_a_voltage_that_the_bias_does_not_move);
    KT_RUN(test_d_sense_is_unanswered_below_the_second_generation);
    KT_RUN(test_e_the_supply_index_goes_in_both_bytes);
    KT_RUN(test_f_sense_refuses_the_first_generation_without_asking);
    KT_RUN(test_g_sense_refuses_an_undeclared_readout);

    mocks_fini();
    return kt_summary();
}
