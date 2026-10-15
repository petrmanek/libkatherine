/**
 * \file
 * \brief Registers of the acquisition unit and of the trigger generator: the
 *   GPIO channels, the shutter delays and the generated pulses.
 *
 * All three are words written to and read from a register bank, so all three
 * are checked against one mock readout that speaks nothing but those banks.
 *
 * \author Petr Mánek
 * \date 18.9.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

// katherine/katherine.h must precede kthread.h on Windows; see
// test_cmd_transact.c, which documents the include order.
#include <katherine/katherine.h>
#ifdef KATHERINE_TEST_EMULATOR
#include <katherine/emulator.h>
#endif

#include "protocol/cmd_builder.h"
#include "protocol/cmd_interface.h"
#include "kthread.h"
#include "ktest.h"

// The readout multiplexes a mapping onto an output channel by its value
// minus one, in a five-bit field, so the last mapping has to be the widest
// value that field holds. A mapping added past it would silently truncate.
_Static_assert(KATHERINE_GPIO_HIGH == 32, "the mapping enumeration must end at the constant high");

#define HW_TYPE_GEN1      0x01
#define HW_TYPE_GEN2      0x03

// A period the tick unit divides exactly, so the expected frequency is a
// round number: half a million ticks of 10 ns is 5 ms, hence 200 Hz.
#define HIGH_TICKS        300000u
#define LOW_TICKS         200000u
#define PERIOD_HZ         200.0
#define DUTY              0.6

// ------------------------------------------------------------------
// A mock readout that speaks the acquisition unit's two commands: it
// acknowledges a write and answers a read out of a register image the case
// fills in, recording every command it was sent. Enough of a peer to check
// what the library composes and how it reads an answer back, which is the
// part worth pinning; what a real unit does with those words is the
// emulator's business, and is checked against it below.

#define PORT_DEVICE       42640
#define PORT_MOCK         42641
#define PORT_DEVICE_QUIET 42642
#define PORT_MOCK_QUIET   42643

#define MOCK_TIMEOUT_MS   5000
#define QUIET_TIMEOUT_MS  100
#define DEVICE_TIMEOUT_MS 2000

#define MAX_CAPTURED      16

#define ACQ_UNIT_REGS     32

static katherine_udp_t g_mock;
static katherine_udp_t g_mock_quiet;
static uint8_t g_captured[MAX_CAPTURED][KATHERINE_CMD_CRD_SIZE];
static unsigned g_captured_count;
static unsigned g_expect;
static uint32_t g_reg[ACQ_UNIT_REGS];

static uint32_t
load_word(const uint8_t *b)
{
    return (uint32_t) b[0] | ((uint32_t) b[1] << 8) | ((uint32_t) b[2] << 16) | ((uint32_t) b[3] << 24);
}

static void
store_word(uint8_t *b, uint32_t value)
{
    b[0] = (uint8_t) (value & 0xFFu);
    b[1] = (uint8_t) ((value >> 8) & 0xFFu);
    b[2] = (uint8_t) ((value >> 16) & 0xFFu);
    b[3] = (uint8_t) ((value >> 24) & 0xFFu);
}

static void *
mock_readout(void *arg)
{
    (void) arg;

    for (unsigned n = 0; n < g_expect && n < MAX_CAPTURED; ++n) {
        uint8_t *cmd = g_captured[n];

        if (katherine_udp_recv_exact(&g_mock, cmd, KATHERINE_CMD_CRD_SIZE) != 0) return NULL;
        ++g_captured_count;

        uint8_t crd[KATHERINE_CMD_CRD_SIZE] = {0};
        const uint8_t reg                   = cmd[KATHERINE_CMD_SUBIDX_BYTE];

        crd[KATHERINE_CMD_OPCODE_BYTE] = cmd[KATHERINE_CMD_OPCODE_BYTE];
        crd[KATHERINE_CMD_SUBIDX_BYTE] = reg;
        // A generator read-back answers under the acquisition unit's
        // identifier rather than its own, as the firmware does.
        if (cmd[KATHERINE_CMD_OPCODE_BYTE] == 0x24) crd[KATHERINE_CMD_OPCODE_BYTE] = 0x22;

        if (crd[KATHERINE_CMD_OPCODE_BYTE] == 0x22) {
            store_word(crd, g_reg[reg % ACQ_UNIT_REGS]);
        }

        if (katherine_udp_send_exact(&g_mock, crd, sizeof(crd)) != 0) return NULL;
    }

    return NULL;
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

// Arms the mock for `commands` exchanges and starts it.
static void
mock_start(kthread_t *thread, unsigned commands)
{
    memset(g_captured, 0xAA, sizeof(g_captured));
    g_captured_count = 0;
    g_expect         = commands;
    KT_REQUIRE(kthread_start(thread, mock_readout, NULL) == 0);
}

static void
expect_nothing_sent(void)
{
    uint8_t buf[KATHERINE_CMD_CRD_SIZE];
    size_t count = sizeof(buf);

    KT_CHECK_EQ(katherine_udp_recv(&g_mock_quiet, buf, &count), KATHERINE_E_TIMEOUT);
}

// ------------------------------------------------------------------

// a) Every mapping has a name of its own, which is what makes a value read
// back off a readout reportable.
static void
test_a_every_mapping_is_named(void)
{
    for (unsigned m = 0; m <= KATHERINE_GPIO_HIGH; ++m) {
        const char *name = katherine_str_gpio_mapping((katherine_gpio_mapping_t) m);

        KT_CHECK(strcmp(name, "unknown") != 0);

        // And no two of them share a name.
        for (unsigned n = 0; n < m; ++n) {
            KT_CHECK(strcmp(name, katherine_str_gpio_mapping((katherine_gpio_mapping_t) n)) != 0);
        }
    }

    KT_CHECK_STR_EQ(katherine_str_gpio_mapping((katherine_gpio_mapping_t) (KATHERINE_GPIO_HIGH + 1)),
        "unknown");
    KT_CHECK_STR_EQ(katherine_str_gpio_mapping((katherine_gpio_mapping_t) -1), "unknown");

    // The three names a probe reports are distinct too.
    KT_CHECK_STR_EQ(katherine_str_gpio_monitor_signal(KATHERINE_GPIO_MONITOR_SIGNAL_CONSTANT), "constant");
    KT_CHECK_STR_EQ(katherine_str_gpio_monitor_signal(KATHERINE_GPIO_MONITOR_SIGNAL_PERIODIC), "periodic");
    KT_CHECK_STR_EQ(katherine_str_gpio_monitor_signal((katherine_gpio_monitor_signal_t) 99), "unknown");
}

// b) Frequency and duty cycle follow from the two tick counts, and are zero
// where the readout measured no period rather than dividing by it.
static void
test_b_frequency_and_duty_come_from_the_tick_counts(void)
{
    katherine_gpio_channel_monitor_t v = {
        .level      = true,
        .signal     = KATHERINE_GPIO_MONITOR_SIGNAL_PERIODIC,
        .high_ticks = HIGH_TICKS,
        .low_ticks  = LOW_TICKS,
    };

    KT_CHECK_NEAR(katherine_gpio_channel_frequency(&v), PERIOD_HZ, 1e-9);
    KT_CHECK_NEAR(katherine_gpio_channel_duty_cycle(&v), DUTY, 1e-9);

    // A channel the readout does not time carries counts that mean nothing,
    // so neither is reported.
    v.signal = KATHERINE_GPIO_MONITOR_SIGNAL_UNKNOWN;
    KT_CHECK_EQ(katherine_gpio_channel_frequency(&v), 0);
    KT_CHECK_EQ(katherine_gpio_channel_duty_cycle(&v), 0);

    // And a period of zero ticks is not divided by.
    v.signal     = KATHERINE_GPIO_MONITOR_SIGNAL_PERIODIC;
    v.high_ticks = 0;
    v.low_ticks  = 0;
    KT_CHECK_EQ(katherine_gpio_channel_frequency(&v), 0);
    KT_CHECK_EQ(katherine_gpio_channel_duty_cycle(&v), 0);
}

// c) A setup composes three words: the directions with the multiplexer
// fields of the first two channels, the other six channels' fields, and the
// terminations. The expected words are written out as hexadecimal rather
// than recomputed from the same shifts the library uses, so a wrong shift is
// a failure and not a matching mistake made twice. Then again on a readout
// with half the channels, where the absent half composes nothing.
static void
test_c_a_setup_composes_three_register_words(void)
{
    katherine_device_t device;
    kthread_t thread;
    katherine_gpio_config_t config;

    memset(&config, 0, sizeof(config));

    // Channel 0 reads, terminated. Channel 1 drives the shutter (mapping 21,
    // so multiplexer field 20). Channel 2 drives a constant high, the widest
    // field there is (mapping 32, field 31). Channel 7 repeats channel 0's
    // level (mapping 6, field 5).
    config.channel[0].mapping     = KATHERINE_GPIO_INPUT;
    config.channel[0].termination = true;
    config.channel[1].mapping     = KATHERINE_GPIO_SHUTTER;
    config.channel[2].mapping     = KATHERINE_GPIO_HIGH;
    config.channel[7].mapping     = KATHERINE_GPIO_LEVEL_0;

    KT_REQUIRE(device_init(&device, HW_TYPE_GEN2, PORT_DEVICE, PORT_MOCK) == 0);
    mock_start(&thread, 3);

    KT_CHECK_EQ(katherine_set_gpio_config(&device, &config), 0);
    KT_CHECK_EQ(kthread_join(&thread), 0);
    KT_REQUIRE(g_captured_count == 3);

    // Directions: channels 1, 2 and 7 drive, so bits 9, 10 and 15 -- 0x8600.
    // Channel 0's field is zero, channel 1's is 20 at bit 21 -- 0x02800000.
    KT_CHECK_EQ(g_captured[0][KATHERINE_CMD_OPCODE_BYTE], 0x21);
    KT_CHECK_EQ(g_captured[0][KATHERINE_CMD_SUBIDX_BYTE], 0x09);
    KT_CHECK_EQ(load_word(g_captured[0]), 0x02808600u);

    // Channel 2's field is 31 at bit 0, channel 7's is 5 at bit 25:
    // 0x0A00001F.
    KT_CHECK_EQ(g_captured[1][KATHERINE_CMD_OPCODE_BYTE], 0x21);
    KT_CHECK_EQ(g_captured[1][KATHERINE_CMD_SUBIDX_BYTE], 0x0A);
    KT_CHECK_EQ(load_word(g_captured[1]), 0x0A00001Fu);

    KT_CHECK_EQ(g_captured[2][KATHERINE_CMD_OPCODE_BYTE], 0x21);
    KT_CHECK_EQ(g_captured[2][KATHERINE_CMD_SUBIDX_BYTE], 0x16);
    KT_CHECK_EQ(load_word(g_captured[2]), 0x00000001u);

    katherine_udp_fini(&device.control_socket);

    // A channel the readout does not have takes its default, whatever the
    // caller asked for. A first-generation readout has four, so everything
    // on channels 4 to 7 composes nothing -- out of range included.
    memset(&config, 0, sizeof(config));
    config.channel[1].mapping     = KATHERINE_GPIO_SHUTTER;
    config.channel[4].mapping     = KATHERINE_GPIO_HIGH;
    config.channel[5].mapping     = KATHERINE_GPIO_CLOCK;
    config.channel[6].termination = true;
    config.channel[7].mapping     = (katherine_gpio_mapping_t) (KATHERINE_GPIO_HIGH + 1);

    KT_REQUIRE(device_init(&device, HW_TYPE_GEN1, PORT_DEVICE, PORT_MOCK) == 0);
    mock_start(&thread, 3);

    KT_CHECK_EQ(katherine_set_gpio_config(&device, &config), 0);
    KT_CHECK_EQ(kthread_join(&thread), 0);
    KT_REQUIRE(g_captured_count == 3);

    // Only channel 1 drives: direction bit 9, its field 20 at bit 21.
    KT_CHECK_EQ(load_word(g_captured[0]), 0x02800200u);
    KT_CHECK_EQ(load_word(g_captured[1]), 0x00000000u);
    KT_CHECK_EQ(load_word(g_captured[2]), 0x00000000u);

    katherine_udp_fini(&device.control_socket);
}

// d) The ready signal is one word: the channels that must be high in its low
// byte, the channels that must be low in the next. That ordering is the
// acquisition unit's, whose own logic conjoins bits 0 to 7 with the levels
// and bits 8 to 15 with their negation. Then again on a readout that
// disregards the absent channels and the fields this call never sends.
static void
test_d_the_ready_signal_is_one_word_of_two_masks(void)
{
    katherine_device_t device;
    kthread_t thread;
    katherine_gpio_config_t config;

    memset(&config, 0, sizeof(config));
    config.channel[0].ready = KATHERINE_GPIO_READY_WHEN_HIGH;
    config.channel[3].ready = KATHERINE_GPIO_READY_WHEN_LOW;
    config.channel[7].ready = KATHERINE_GPIO_READY_WHEN_HIGH;

    KT_REQUIRE(device_init(&device, HW_TYPE_GEN2, PORT_DEVICE, PORT_MOCK) == 0);
    mock_start(&thread, 1);

    KT_CHECK_EQ(katherine_set_composed_ready_signal(&device, &config), 0);
    KT_CHECK_EQ(kthread_join(&thread), 0);
    KT_REQUIRE(g_captured_count == 1);

    KT_CHECK_EQ(g_captured[0][KATHERINE_CMD_SUBIDX_BYTE], 0x13);
    KT_CHECK_EQ(load_word(g_captured[0]), 0x00000881u);

    katherine_udp_fini(&device.control_socket);

    // The channels a first-generation readout does not have take no part.
    memset(&config, 0, sizeof(config));
    config.channel[0].ready = KATHERINE_GPIO_READY_WHEN_HIGH;
    config.channel[5].ready = KATHERINE_GPIO_READY_WHEN_LOW;

    KT_REQUIRE(device_init(&device, HW_TYPE_GEN1, PORT_DEVICE, PORT_MOCK) == 0);
    mock_start(&thread, 1);

    KT_CHECK_EQ(katherine_set_composed_ready_signal(&device, &config), 0);
    KT_CHECK_EQ(kthread_join(&thread), 0);
    KT_REQUIRE(g_captured_count == 1);
    KT_CHECK_EQ(load_word(g_captured[0]), 0x00000001u);

    katherine_udp_fini(&device.control_socket);

    // And a termination katherine_set_gpio_config() would refuse -- channel
    // 4 of a readout whose first four reach a connector -- is none of this
    // call's business.
    memset(&config, 0, sizeof(config));
    config.channel[0].ready       = KATHERINE_GPIO_READY_WHEN_HIGH;
    config.channel[4].termination = true;

    KT_REQUIRE(device_init(&device, HW_TYPE_GEN2, PORT_DEVICE, PORT_MOCK) == 0);
    mock_start(&thread, 1);

    KT_CHECK_EQ(katherine_set_composed_ready_signal(&device, &config), 0);
    KT_CHECK_EQ(kthread_join(&thread), 0);
    KT_REQUIRE(g_captured_count == 1);
    KT_CHECK_EQ(load_word(g_captured[0]), 0x00000001u);

    katherine_udp_fini(&device.control_socket);
}

// e) A setting the readout cannot carry out is refused before anything is
// sent. Each call refuses only the fields it applies: a termination the
// readout cannot carry out is none of the ready signal's business.
static void
test_e_settings_the_readout_cannot_carry_are_refused(void)
{
    katherine_device_t device;
    katherine_gpio_config_t config;

    // A second-generation readout has eight channels, of which the first
    // four reach a connector and can be terminated.
    KT_REQUIRE(device_init(&device, HW_TYPE_GEN2, PORT_DEVICE_QUIET, PORT_MOCK_QUIET) == 0);

    memset(&config, 0, sizeof(config));
    config.channel[4].termination = true;
    KT_CHECK_EQ(katherine_set_gpio_config(&device, &config), KATHERINE_E_INVAL);
    expect_nothing_sent();

    memset(&config, 0, sizeof(config));
    config.channel[0].mapping = (katherine_gpio_mapping_t) (KATHERINE_GPIO_HIGH + 1);
    KT_CHECK_EQ(katherine_set_gpio_config(&device, &config), KATHERINE_E_INVAL);
    expect_nothing_sent();

    memset(&config, 0, sizeof(config));
    config.channel[0].ready = (katherine_gpio_ready_t) (KATHERINE_GPIO_READY_WHEN_LOW + 1);
    KT_CHECK_EQ(katherine_set_composed_ready_signal(&device, &config), KATHERINE_E_INVAL);
    expect_nothing_sent();

    // And an undeclared readout is refused for not knowing how many it has.
    katherine_udp_fini(&device.control_socket);
    KT_REQUIRE(device_init(&device, 0, PORT_DEVICE_QUIET, PORT_MOCK_QUIET) == 0);

    memset(&config, 0, sizeof(config));
    KT_CHECK_EQ(katherine_set_gpio_config(&device, &config), KATHERINE_E_STATE);
    KT_CHECK_EQ(katherine_set_composed_ready_signal(&device, &config), KATHERINE_E_STATE);
    KT_CHECK_EQ(katherine_probe_gpio(&device, &(katherine_gpio_monitor_t) {0}), KATHERINE_E_STATE);
    expect_nothing_sent();

    katherine_udp_fini(&device.control_socket);
}

// f) A probe reads every channel's level out of one register, and the tick
// counters of the four the readout times -- but only of those whose counters
// it says are valid. A channel with no valid counters is constant, and one
// past the fourth is not measured at all.
static void
test_f_a_probe_reads_levels_and_only_the_valid_counters(void)
{
    katherine_device_t device;
    katherine_gpio_monitor_t state;
    kthread_t thread;

    // Levels on channels 1, 2 and 7; the counters of channel 1 valid, so
    // three of the timed four are constant.
    memset(g_reg, 0, sizeof(g_reg));
    g_reg[0x09] = (1u << 29) | 0x86u;
    g_reg[0x0D] = HIGH_TICKS;
    g_reg[0x0E] = LOW_TICKS;

    KT_REQUIRE(device_init(&device, HW_TYPE_GEN2, PORT_DEVICE, PORT_MOCK) == 0);
    mock_start(&thread, 3);

    KT_CHECK_EQ(katherine_probe_gpio(&device, &state), 0);
    KT_CHECK_EQ(kthread_join(&thread), 0);

    // One read of the level register and two of channel 1's counters: the
    // counters of a constant channel are not asked for.
    KT_CHECK_EQ(g_captured_count, 3);
    KT_CHECK_EQ(g_captured[0][KATHERINE_CMD_OPCODE_BYTE], 0x22);
    KT_CHECK_EQ(g_captured[0][KATHERINE_CMD_SUBIDX_BYTE], 0x09);
    KT_CHECK_EQ(g_captured[1][KATHERINE_CMD_SUBIDX_BYTE], 0x0D);
    KT_CHECK_EQ(g_captured[2][KATHERINE_CMD_SUBIDX_BYTE], 0x0E);

    KT_CHECK_EQ(state.channel[0].level, false);
    KT_CHECK_EQ(state.channel[1].level, true);
    KT_CHECK_EQ(state.channel[2].level, true);
    KT_CHECK_EQ(state.channel[7].level, true);

    KT_CHECK_EQ(state.channel[1].signal, KATHERINE_GPIO_MONITOR_SIGNAL_PERIODIC);
    KT_CHECK_EQ(state.channel[1].high_ticks, HIGH_TICKS);
    KT_CHECK_EQ(state.channel[1].low_ticks, LOW_TICKS);
    KT_CHECK_NEAR(katherine_gpio_channel_frequency(&state.channel[1]), PERIOD_HZ, 1e-9);

    KT_CHECK_EQ(state.channel[0].signal, KATHERINE_GPIO_MONITOR_SIGNAL_CONSTANT);
    KT_CHECK_EQ(state.channel[3].signal, KATHERINE_GPIO_MONITOR_SIGNAL_CONSTANT);
    KT_CHECK_EQ(state.channel[0].high_ticks, 0);

    // Past the fourth there is no register to read, so nothing is claimed.
    KT_CHECK_EQ(state.channel[4].signal, KATHERINE_GPIO_MONITOR_SIGNAL_UNKNOWN);
    KT_CHECK_EQ(state.channel[7].signal, KATHERINE_GPIO_MONITOR_SIGNAL_UNKNOWN);

    katherine_udp_fini(&device.control_socket);
}

#ifdef KATHERINE_TEST_EMULATOR
// g) And the emulated readout composes that register the same way, from the
// pins its profile describes -- so the layout the case above reads is
// checked against a model of the unit as well as against a crafted word.
static void
test_g_the_emulated_unit_composes_the_level_register(void)
{
    katherine_emu_t emu;
    katherine_emu_profile_t profile;
    uint8_t crd[KATHERINE_EMU_CRD_SIZE];
    size_t len;

    katherine_emu_profile_defaults(&profile);
    profile.gpio[1].level      = true;
    profile.gpio[1].high_ticks = HIGH_TICKS;
    profile.gpio[1].low_ticks  = LOW_TICKS;
    profile.gpio[2].level      = true;
    profile.gpio[7].level      = true;
    KT_REQUIRE(katherine_emu_init(&emu, &profile) == 0);

    katherine_cmd_t read = katherine_cmd_create((uint8_t) CMD_TYPE_GET_ACQUISITION_UNIT_DATA);
    katherine_cmd_payload_set_subidx(&read, 0x09);
    KT_REQUIRE(katherine_emu_cmd_in(&emu, read.b, sizeof(read.b)) == 0);
    KT_CHECK_EQ(katherine_emu_crd_out(&emu, crd, &len), 0);

    // The same word the case above crafted: levels on 1, 2 and 7, and only
    // channel 1's counters valid.
    KT_CHECK_EQ(load_word(crd), (1u << 29) | 0x86u);
    KT_CHECK_EQ(crd[KATHERINE_CMD_SUBIDX_BYTE], 0x09);

    katherine_cmd_payload_set_subidx(&read, 0x0D);
    KT_REQUIRE(katherine_emu_cmd_in(&emu, read.b, sizeof(read.b)) == 0);
    KT_CHECK_EQ(katherine_emu_crd_out(&emu, crd, &len), 0);
    KT_CHECK_EQ(load_word(crd), HIGH_TICKS);

    katherine_emu_fini(&emu);
}

#endif /* KATHERINE_TEST_EMULATOR */

// h) The shutter delays are two registers of their own. The caller gives
// seconds and the library turns them into the counts the readout keeps, so
// the check is that a microsecond and two and a half milliseconds arrive as
// 100 and 250000 -- which is the ten-nanosecond tick, asserted through the
// API rather than against a constant the caller can no longer see.
static void
test_h_the_shutter_delays_are_two_registers(void)
{
    katherine_device_t device;
    kthread_t thread;

    KT_REQUIRE(device_init(&device, HW_TYPE_GEN2, PORT_DEVICE, PORT_MOCK) == 0);
    mock_start(&thread, 2);

    KT_CHECK_EQ(katherine_set_shutter_delays(&device, 1e-6, 2.5e-3), 0);
    KT_CHECK_EQ(kthread_join(&thread), 0);
    KT_REQUIRE(g_captured_count == 2);

    KT_CHECK_EQ(g_captured[0][KATHERINE_CMD_OPCODE_BYTE], 0x21);
    KT_CHECK_EQ(g_captured[0][KATHERINE_CMD_SUBIDX_BYTE], 0x06);
    KT_CHECK_EQ(load_word(g_captured[0]), 100);

    KT_CHECK_EQ(g_captured[1][KATHERINE_CMD_SUBIDX_BYTE], 0x07);
    KT_CHECK_EQ(load_word(g_captured[1]), 250000);

    katherine_udp_fini(&device.control_socket);

    // Rounding is to the nearest count, not towards zero: four nanoseconds
    // more than a microsecond is still 100 counts, six more is 101.
    KT_REQUIRE(device_init(&device, HW_TYPE_GEN2, PORT_DEVICE, PORT_MOCK) == 0);
    mock_start(&thread, 2);
    KT_CHECK_EQ(katherine_set_shutter_delays(&device, 1.004e-6, 1.006e-6), 0);
    KT_CHECK_EQ(kthread_join(&thread), 0);
    KT_REQUIRE(g_captured_count == 2);
    KT_CHECK_EQ(load_word(g_captured[0]), 100);
    KT_CHECK_EQ(load_word(g_captured[1]), 101);

    katherine_udp_fini(&device.control_socket);

    // A delay the register cannot hold is refused before anything is sent,
    // and so is a negative one and a NaN. The widest it holds is 2^32-1
    // counts of ten nanoseconds, a touch under 43 seconds.
    KT_REQUIRE(device_init(&device, HW_TYPE_GEN2, PORT_DEVICE_QUIET, PORT_MOCK_QUIET) == 0);

    KT_CHECK_EQ(katherine_set_shutter_delays(&device, 43.0, 0.0), KATHERINE_E_INVAL);
    expect_nothing_sent();

    KT_CHECK_EQ(katherine_set_shutter_delays(&device, -1e-9, 0.0), KATHERINE_E_INVAL);
    expect_nothing_sent();

    KT_CHECK_EQ(katherine_set_shutter_delays(&device, 0.0, (double) NAN), KATHERINE_E_INVAL);
    expect_nothing_sent();

    katherine_udp_fini(&device.control_socket);
}

// i) The generator takes eleven writes, and the enable word comes last:
// writing it is what starts the generator, so everything it acts on has to be
// in place already.
static void
test_i_the_generator_is_enabled_last(void)
{
    katherine_device_t device;
    katherine_pulse_config_t config;
    kthread_t thread;

    memset(&config, 0, sizeof(config));

    // The internal channel pulses, channel 2 passes the input through
    // inverted, and the oscillator stands in for the trigger.
    config.channel[0].enabled        = true;
    config.channel[0].length_seconds = 5e-6; // 500 counts of 10 ns
    config.channel[0].delay_seconds  = 250e-9;
    config.channel[2].enabled        = true;
    config.channel[2].output         = KATHERINE_PULSE_OUTPUT_PASSTHROUGH_INVERTED;
    config.only_when_ready           = true;
    config.source                    = KATHERINE_PULSE_SOURCE_OSCILLATOR;
    config.oscillator_frequency_hz   = 1e6; // 50 MHz over this is a divisor of 50

    KT_REQUIRE(device_init(&device, HW_TYPE_GEN2, PORT_DEVICE, PORT_MOCK) == 0);
    mock_start(&thread, 11);

    KT_CHECK_EQ(katherine_set_pulse_generator(&device, &config), 0);
    KT_CHECK_EQ(kthread_join(&thread), 0);
    KT_REQUIRE(g_captured_count == 11);

    // The setup word: channel 2's output is 3 at bit 4 and the ready gate
    // at bit 8. The edge bit stays clear, the source being the oscillator.
    KT_CHECK_EQ(g_captured[0][KATHERINE_CMD_OPCODE_BYTE], 0x23);
    KT_CHECK_EQ(g_captured[0][KATHERINE_CMD_SUBIDX_BYTE], 0x01);
    KT_CHECK_EQ(load_word(g_captured[0]), 0x00000130u);

    // Then a length and a delay per channel, registers 2 through 9.
    KT_CHECK_EQ(g_captured[1][KATHERINE_CMD_SUBIDX_BYTE], 0x02);
    KT_CHECK_EQ(load_word(g_captured[1]), 500);
    KT_CHECK_EQ(g_captured[2][KATHERINE_CMD_SUBIDX_BYTE], 0x03);
    KT_CHECK_EQ(load_word(g_captured[2]), 25);
    KT_CHECK_EQ(g_captured[8][KATHERINE_CMD_SUBIDX_BYTE], 0x09);

    // The oscillator divisor, and the enable word last of all: the generator
    // bit, the oscillator bit, and channel 2's output gate at bit 1.
    KT_CHECK_EQ(g_captured[9][KATHERINE_CMD_SUBIDX_BYTE], 0x0E);
    KT_CHECK_EQ(load_word(g_captured[9]), 50);
    KT_CHECK_EQ(g_captured[10][KATHERINE_CMD_SUBIDX_BYTE], 0x00);
    KT_CHECK_EQ(load_word(g_captured[10]), 0xC0000002u);

    katherine_udp_fini(&device.control_socket);
}

// i2) Channels the readout does not have take their defaults, whatever the
// caller asked for, as they do for GPIO and for the timing unit. Every
// supported readout carries all four today, so the count is forced here to
// exercise the bound rather than wait for a model that trips it.
static void
test_i2_channels_the_generator_lacks_take_their_defaults(void)
{
    katherine_device_t device;
    katherine_pulse_config_t config;
    kthread_t thread;

    memset(&config, 0, sizeof(config));
    config.channel[0].enabled        = true;
    config.channel[0].length_seconds = 5e-6;
    config.channel[3].enabled        = true;
    config.channel[3].output         = KATHERINE_PULSE_OUTPUT_PASSTHROUGH_INVERTED;
    config.channel[3].delay_seconds  = 1.0; // would be refused if it were read

    KT_REQUIRE(device_init(&device, HW_TYPE_GEN2, PORT_DEVICE, PORT_MOCK) == 0);
    device.derived_info.pulse_channel_count = 2;

    // Two channels, so five writes rather than eleven: the setup word, a
    // length and a delay each, and the divisor and enable words.
    mock_start(&thread, 7);

    KT_CHECK_EQ(katherine_set_pulse_generator(&device, &config), 0);
    KT_CHECK_EQ(kthread_join(&thread), 0);
    KT_REQUIRE(g_captured_count == 7);

    // Channel 3 contributes nothing: no output field, no enable gate, and
    // its out-of-range delay never reaches a register.
    KT_CHECK_EQ(load_word(g_captured[0]), 0x00000000u);
    KT_CHECK_EQ(g_captured[6][KATHERINE_CMD_SUBIDX_BYTE], 0x00);
    KT_CHECK_EQ(load_word(g_captured[6]), 0x80000000u);

    katherine_udp_fini(&device.control_socket);
}

// j) A delay longer than its register is refused rather than truncated, and
// a first-generation readout is refused the command outright.
static void
test_j_the_generator_refuses_what_it_cannot_carry(void)
{
    katherine_device_t device;
    katherine_pulse_config_t config;
    katherine_pulse_counts_t counts;

    KT_REQUIRE(device_init(&device, HW_TYPE_GEN2, PORT_DEVICE_QUIET, PORT_MOCK_QUIET) == 0);

    // The delay register is 24 bits of 10 ns, so it tops out a touch under
    // 0.168 s; a delay past that is refused rather than truncated.
    memset(&config, 0, sizeof(config));
    config.channel[1].delay_seconds = 0.17;
    KT_CHECK_EQ(katherine_set_pulse_generator(&device, &config), KATHERINE_E_INVAL);
    expect_nothing_sent();

    config.channel[1].delay_seconds = -1e-9;
    KT_CHECK_EQ(katherine_set_pulse_generator(&device, &config), KATHERINE_E_INVAL);
    expect_nothing_sent();

    // An oscillator rate the divisor cannot reach is refused too: the
    // divisor is at least one, so the rate cannot exceed the oscillator.
    memset(&config, 0, sizeof(config));
    config.source                  = KATHERINE_PULSE_SOURCE_OSCILLATOR;
    config.oscillator_frequency_hz = 60e6;
    KT_CHECK_EQ(katherine_set_pulse_generator(&device, &config), KATHERINE_E_INVAL);
    expect_nothing_sent();

    memset(&config, 0, sizeof(config));
    config.channel[0].output = (katherine_pulse_output_t) 4;
    KT_CHECK_EQ(katherine_set_pulse_generator(&device, &config), KATHERINE_E_INVAL);
    expect_nothing_sent();

    katherine_udp_fini(&device.control_socket);

    KT_REQUIRE(device_init(&device, HW_TYPE_GEN1, PORT_DEVICE_QUIET, PORT_MOCK_QUIET) == 0);
    memset(&config, 0, sizeof(config));
    KT_CHECK_EQ(katherine_set_pulse_generator(&device, &config), KATHERINE_E_UNSUPPORTED);
    KT_CHECK_EQ(katherine_get_pulse_generator_counts(&device, &counts), KATHERINE_E_UNSUPPORTED);
    KT_CHECK_EQ(katherine_reset_pulse_generator_counts(&device), KATHERINE_E_UNSUPPORTED);
    expect_nothing_sent();

    katherine_udp_fini(&device.control_socket);
}

// k) The probe reads four registers, one of which is the generator's own
// clock -- so the tick this library documents can be checked against what
// the readout says rather than taken on trust.
static void
test_k_the_generator_reports_its_own_clock(void)
{
    katherine_device_t device;
    katherine_pulse_counts_t state;
    kthread_t thread;

    memset(g_reg, 0, sizeof(g_reg));
    g_reg[0x0A] = 1234;
    g_reg[0x0B] = 5678;
    g_reg[0x0C] = 42;
    g_reg[0x0D] = 100;

    KT_REQUIRE(device_init(&device, HW_TYPE_GEN2, PORT_DEVICE, PORT_MOCK) == 0);
    mock_start(&thread, 4);

    KT_CHECK_EQ(katherine_get_pulse_generator_counts(&device, &state), 0);
    KT_CHECK_EQ(kthread_join(&thread), 0);

    // The counts come back as seconds: 1234 and 5678 counts of ten
    // nanoseconds, which is the resolution the call documents and which the
    // readout's own clock register corroborates below.
    KT_CHECK(fabs(state.low_seconds - 12.34e-6) < 1e-12);
    KT_CHECK(fabs(state.high_seconds - 56.78e-6) < 1e-12);
    KT_CHECK_EQ(state.rising_edges, 42);

    // A hundred megahertz is one tick of ten nanoseconds, so the readout
    // says the same resolution the conversion above assumed.
    KT_CHECK_EQ(state.clock_mhz, 100);

    katherine_udp_fini(&device.control_socket);
}

// l) And the counter is reset by writing zero to the register it is read
// from, which is why the call takes no value.
static void
test_l_the_edge_counter_is_reset_by_writing_zero(void)
{
    katherine_device_t device;
    kthread_t thread;

    KT_REQUIRE(device_init(&device, HW_TYPE_GEN2, PORT_DEVICE, PORT_MOCK) == 0);
    mock_start(&thread, 1);

    KT_CHECK_EQ(katherine_reset_pulse_generator_counts(&device), 0);
    KT_CHECK_EQ(kthread_join(&thread), 0);

    KT_CHECK_EQ(g_captured[0][KATHERINE_CMD_OPCODE_BYTE], 0x23);
    KT_CHECK_EQ(g_captured[0][KATHERINE_CMD_SUBIDX_BYTE], 0x0C);
    KT_CHECK_EQ(load_word(g_captured[0]), 0);

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

    KT_RUN(test_a_every_mapping_is_named);
    KT_RUN(test_b_frequency_and_duty_come_from_the_tick_counts);
    KT_RUN(test_c_a_setup_composes_three_register_words);
    KT_RUN(test_d_the_ready_signal_is_one_word_of_two_masks);
    KT_RUN(test_e_settings_the_readout_cannot_carry_are_refused);
    KT_RUN(test_f_a_probe_reads_levels_and_only_the_valid_counters);
    KT_RUN(test_h_the_shutter_delays_are_two_registers);
    KT_RUN(test_i_the_generator_is_enabled_last);
    KT_RUN(test_i2_channels_the_generator_lacks_take_their_defaults);
    KT_RUN(test_j_the_generator_refuses_what_it_cannot_carry);
    KT_RUN(test_k_the_generator_reports_its_own_clock);
    KT_RUN(test_l_the_edge_counter_is_reset_by_writing_zero);
#ifdef KATHERINE_TEST_EMULATOR
    KT_RUN(test_g_the_emulated_unit_composes_the_level_register);
#endif

    mocks_fini();
    return kt_summary();
}
