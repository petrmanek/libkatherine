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
 * The cases here drive the emulated readout in memory, with no sockets, so
 * that the model of the peer is what is asserted against.
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
#include <string.h>

#include <katherine/katherine.h>
#include <katherine/emulator.h>

#include "protocol/cmd_builder.h"
#include "protocol/cmd_interface.h"
#include "ktest.h"

// Hardware types of the two generations, as c/src/device/hw_info.c maps them.
#define HW_TYPE_GEN1     0x01
#define HW_TYPE_GEN2     0x03

// One step of the readout's bias converter, 2.5 V over 12 bits through the
// divider and its compensation. Read-backs land on a multiple of it.
#define BIAS_STEP_V      0.50473f

// Bias setpoint the cases work at: positive, as a hole-collecting sensor
// needs, and far enough from zero that a quantization error cannot be
// confused with the sign.
#define BIAS_SET_V       150.0f

// Converter steps between the read-backs of that setpoint and of zero, as
// the lab readout answered for the same change.
#define BIAS_SET_V_STEPS 297

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

int
main(void)
{
    KT_RUN(test_a_read_back_is_the_setpoint_quantized);
    KT_RUN(test_b_read_back_answers_on_both_generations);
    KT_RUN(test_c_sense_is_a_voltage_that_the_bias_does_not_move);
    KT_RUN(test_d_sense_is_unanswered_below_the_second_generation);
    return kt_summary();
}
