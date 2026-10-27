/**
 * \file
 * \brief The readout's internal timing unit.
 *
 * Two commands whose shape differs by generation and whose answers carry no
 * identifier of their own, so what is worth pinning is the word that goes out
 * and the count of datagrams that comes back.
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
#include <string.h>

// katherine/katherine.h must precede kthread.h on Windows; see
// test_cmd_transact.c, which documents the include order.
#include <katherine/katherine.h>
#include <katherine/transport.h>
#ifdef KATHERINE_TEST_EMULATOR
#include <katherine/emulator.h>
#endif

// Outside the guard above: the mock readout below is sockets and nothing
// else, and it is compiled whether or not the emulator is in the build.
#include "transport/udp.h"

#include "protocol/cmd_builder.h"
#include "protocol/cmd_interface.h"
#include "kthread.h"
#include "kports.h"
#include "ktest.h"

#define HW_TYPE_GEN1      0x01
#define HW_TYPE_GEN2      0x03

#define PORT_DEVICE       (KPORTS_TDC + 0)
#define PORT_MOCK         (KPORTS_TDC + 1)
#define PORT_DEVICE_QUIET (KPORTS_TDC + 2)
#define PORT_MOCK_QUIET   (KPORTS_TDC + 3)

#define MOCK_TIMEOUT_MS   5000
#define QUIET_TIMEOUT_MS  100
#define DEVICE_TIMEOUT_MS 300

#define MAX_CAPTURED      8

// Counts the mock and the emulated readout answer with: distinct per
// channel, so a count filed under the wrong channel is a failure.
#define COUNT_BASE        1000u
#define COUNT_STEP        7u

static katherine_udp_t g_mock;
static katherine_udp_t g_mock_quiet;
static uint8_t g_captured[MAX_CAPTURED][KATHERINE_CMD_CRD_SIZE];
static unsigned g_captured_count;
static unsigned g_replies;

// Firmware 5 writes this command's answers back to back and the USB link
// loses their boundaries, delivering the tail of an answer as a short read of
// its own. Measured on the two-chip Gen2: three whole answers, then a tail
// before each of the remaining two and one after the last. See
// misc/hw-evidence-20261024/README.md.
#define SPLIT_AFTER 3
static unsigned g_split;  // Interleave tails as the readout does over USB.
static unsigned g_shorts; // Send this many tails and no answer at all.

static uint32_t
load_word(const uint8_t *b)
{
    return (uint32_t) b[0] | ((uint32_t) b[1] << 8) | ((uint32_t) b[2] << 16) | ((uint32_t) b[3] << 24);
}

static uint32_t
expected_count(unsigned i)
{
    return COUNT_BASE + COUNT_STEP * i;
}

// Receives one command, records it, and answers it g_replies times: once for
// an acknowledgement, five times for a count sequence, or not at all.
static void *
mock_readout(void *arg)
{
    (void) arg;

    uint8_t *cmd = g_captured[0];
    if (katherine_udp_recv_exact(&g_mock, cmd, KATHERINE_CMD_CRD_SIZE) != 0) return NULL;
    ++g_captured_count;

    // The tail of an answer, which is what a split one arrives as.
    static const uint8_t tail[4] = {0x00, 0x00, 0x33, 0x00};

    for (unsigned i = 0; i < g_shorts; ++i) {
        if (katherine_udp_send_exact(&g_mock, tail, sizeof(tail)) != 0) return NULL;
    }

    for (unsigned i = 0; i < g_replies; ++i) {
        uint8_t crd[KATHERINE_CMD_CRD_SIZE] = {0};

        if (g_split && i >= SPLIT_AFTER) {
            if (katherine_udp_send_exact(&g_mock, tail, sizeof(tail)) != 0) return NULL;
        }

        crd[KATHERINE_CMD_OPCODE_BYTE] = cmd[KATHERINE_CMD_OPCODE_BYTE];
        if (cmd[KATHERINE_CMD_OPCODE_BYTE] == 0x33) {
            const uint32_t count = expected_count(i);

            crd[0] = (uint8_t) (count & 0xFFu);
            crd[1] = (uint8_t) ((count >> 8) & 0xFFu);
            crd[2] = (uint8_t) ((count >> 16) & 0xFFu);
            crd[3] = (uint8_t) ((count >> 24) & 0xFFu);
        }

        if (katherine_udp_send_exact(&g_mock, crd, sizeof(crd)) != 0) return NULL;
    }

    if (g_split && g_replies > SPLIT_AFTER) {
        if (katherine_udp_send_exact(&g_mock, tail, sizeof(tail)) != 0) return NULL;
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
        const katherine_device_info_t info = {.hw_type = hw_type, .chip_count = 1};
        if (katherine_device_declare(device, &info) != KATHERINE_E_OK) return KATHERINE_E_INVAL;
    }

    // The command layer takes the control lock through the device's
    // transport, so a device assembled here needs one of its own.
    const katherine_udp_config_t config = {
        .local_address  = "127.0.0.1",
        .local_port     = local_port,
        .remote_address = "127.0.0.1",
        .remote_port    = remote_port,
        .timeout_ms     = DEVICE_TIMEOUT_MS,
    };

    return katherine_transport_init_udp(&device->control_transport, &config);
}

// Every field the thread reads is set before it is started, not after.
static void
mock_start_with(kthread_t *thread, unsigned replies, unsigned split, unsigned shorts)
{
    memset(g_captured, 0xAA, sizeof(g_captured));
    g_captured_count = 0;
    g_replies        = replies;
    g_split          = split;
    g_shorts         = shorts;
    KT_REQUIRE(kthread_start(thread, mock_readout, NULL) == 0);
}

static void
mock_start(kthread_t *thread, unsigned replies)
{
    mock_start_with(thread, replies, 0, 0);
}

// As above, but answering the way firmware 5 does over USB.
static void
mock_start_split(kthread_t *thread, unsigned replies)
{
    mock_start_with(thread, replies, 1, 0);
}

// As above, but answering with nothing but tails.
static void
mock_start_shorts(kthread_t *thread, unsigned shorts)
{
    mock_start_with(thread, 0, 0, shorts);
}

static void
expect_nothing_sent(void)
{
    uint8_t buf[KATHERINE_CMD_CRD_SIZE];
    size_t count = sizeof(buf);

    KT_CHECK_EQ(katherine_udp_recv(&g_mock_quiet, buf, &count), KATHERINE_E_TIMEOUT);
}

// Channels 0 and 2 enabled, on distinct sources and opposite edges, with
// channel 4 also enabled -- which a first-generation readout does not have.
static void
fill_config(katherine_tdc_config_t *config, bool include_fifth)
{
    memset(config, 0, sizeof(*config));

    config->channel[0].enabled = true;
    config->channel[0].source  = KATHERINE_TDC_SOURCE_GPIO_1;
    config->channel[0].edge    = KATHERINE_TRIGGER_EDGE_FALLING;

    config->channel[2].enabled = true;
    config->channel[2].source  = KATHERINE_TDC_SOURCE_CHIP_3_FEEDBACK;

    if (include_fifth) {
        config->channel[4].enabled = true;
        config->channel[4].source  = KATHERINE_TDC_SOURCE_GPIO_3;
    }
}

// ------------------------------------------------------------------

// a) The second generation's word: one enable bit per channel, a bit that
// turns the unit on as a whole, and a four-bit field per channel carrying
// what it listens to with the edge above it. Written out as hexadecimal
// rather than recomputed from the same shifts the library uses.
static void
test_a_the_second_generation_word(void)
{
    katherine_device_t device;
    katherine_tdc_config_t config;
    kthread_t thread;

    fill_config(&config, true);

    KT_REQUIRE(device_init(&device, HW_TYPE_GEN2, PORT_DEVICE, PORT_MOCK) == 0);
    mock_start(&thread, 1);

    KT_CHECK_EQ(katherine_set_tdc_config(&device, &config), 0);
    KT_CHECK_EQ(kthread_join(&thread), 0);
    KT_REQUIRE(g_captured_count == 1);

    // Enables for channels 0, 2 and 4 plus the global bit: 0x35. Channel 0's
    // field is source 1 with the falling-edge bit, 9, at bit 8; channel 2's
    // is source 7 at bit 16; channel 4's is source 3 at bit 24. The
    // untouched channels contribute nothing.
    KT_CHECK_EQ(g_captured[0][KATHERINE_CMD_OPCODE_BYTE], 0x32);
    KT_CHECK_EQ(load_word(g_captured[0]), 0x03070935u);

    katherine_transport_fini(&device.control_transport);
}

// b) The first generation's is two masks over three channels, and carries no
// source at all -- so the same configuration, minus the channel that
// readout does not have, produces a quite different word.
static void
test_b_the_first_generation_word(void)
{
    katherine_device_t device;
    katherine_tdc_config_t config;
    kthread_t thread;

    fill_config(&config, false);

    KT_REQUIRE(device_init(&device, HW_TYPE_GEN1, PORT_DEVICE, PORT_MOCK) == 0);
    mock_start(&thread, 1);

    KT_CHECK_EQ(katherine_set_tdc_config(&device, &config), 0);
    KT_CHECK_EQ(kthread_join(&thread), 0);
    KT_REQUIRE(g_captured_count == 1);

    // Channels 0 and 2 enabled, channel 0 on the falling edge: enables 0x05
    // in the low byte, edges 0x01 in the next. No global enable bit, and no
    // trace of either source.
    KT_CHECK_EQ(load_word(g_captured[0]), 0x00000105u);

    katherine_transport_fini(&device.control_transport);
}

// c) Enabling a channel the readout's unit does not have is refused rather
// than dropped, as is a source outside the enumeration.
static void
test_c_channels_the_unit_lacks_take_their_defaults(void)
{
    katherine_device_t device;
    katherine_tdc_config_t config;
    kthread_t thread;
    katherine_tdc_counts_t counts = {0};

    // A first-generation unit watches three channels, so everything the
    // configuration says about channel 4 -- its enable, its source, even a
    // source outside the enumeration -- composes nothing at all. The word is
    // the one the same configuration without it produces.
    fill_config(&config, true);
    config.channel[4].source = (katherine_tdc_source_t) (KATHERINE_TDC_SOURCE_CHIP_3_FEEDBACK + 1);

    KT_REQUIRE(device_init(&device, HW_TYPE_GEN1, PORT_DEVICE, PORT_MOCK) == 0);
    mock_start(&thread, 1);

    KT_CHECK_EQ(katherine_set_tdc_config(&device, &config), 0);
    KT_CHECK_EQ(kthread_join(&thread), 0);
    KT_REQUIRE(g_captured_count == 1);
    KT_CHECK_EQ(load_word(g_captured[0]), 0x00000105u);

    katherine_transport_fini(&device.control_transport);

    // A source outside the enumeration on a channel the unit does have is
    // still refused, before anything is sent.
    KT_REQUIRE(device_init(&device, HW_TYPE_GEN1, PORT_DEVICE_QUIET, PORT_MOCK_QUIET) == 0);

    fill_config(&config, false);
    config.channel[1].source = (katherine_tdc_source_t) (KATHERINE_TDC_SOURCE_CHIP_3_FEEDBACK + 1);
    KT_CHECK_EQ(katherine_set_tdc_config(&device, &config), KATHERINE_E_INVAL);
    expect_nothing_sent();

    katherine_transport_fini(&device.control_transport);

    // And an undeclared readout is refused for not knowing how many it has.
    KT_REQUIRE(device_init(&device, 0, PORT_DEVICE_QUIET, PORT_MOCK_QUIET) == 0);
    fill_config(&config, false);
    KT_CHECK_EQ(katherine_set_tdc_config(&device, &config), KATHERINE_E_STATE);
    KT_CHECK_EQ(katherine_get_tdc_counts(&device, &counts), KATHERINE_E_STATE);
    expect_nothing_sent();

    katherine_transport_fini(&device.control_transport);
}

// d) The count sequence is five datagrams, and the fifth completes the call:
// a caller waiting for a sixth spends a receive timeout on every read, which
// is what the reference implementation does.
static void
test_d_five_counts_complete_the_sequence(void)
{
    katherine_device_t device;
    katherine_tdc_counts_t counts = {0};
    kthread_t thread;

    KT_REQUIRE(device_init(&device, HW_TYPE_GEN2, PORT_DEVICE, PORT_MOCK) == 0);
    mock_start(&thread, KATHERINE_MAX_TDC_CHANNEL_COUNT);

    KT_CHECK_EQ(katherine_get_tdc_counts(&device, &counts), 0);
    KT_CHECK_EQ(kthread_join(&thread), 0);

    KT_CHECK_EQ(g_captured[0][KATHERINE_CMD_OPCODE_BYTE], 0x33);
    for (unsigned i = 0; i < KATHERINE_MAX_TDC_CHANNEL_COUNT; ++i) {
        KT_CHECK_EQ(counts.channel[i], expected_count(i));
    }

    katherine_transport_fini(&device.control_transport);

    // A readout with fewer counters than the array holds leaves no stale
    // value behind: the entries it does not answer for read zero, whatever
    // the caller's array held going in.
    for (unsigned i = 0; i < KATHERINE_MAX_TDC_CHANNEL_COUNT; ++i) {
        counts.channel[i] = 0xDEADBEEFu;
    }

    // Gen2, because what is under test is the channel count and only the
    // second generation can report counts at all.
    KT_REQUIRE(device_init(&device, HW_TYPE_GEN2, PORT_DEVICE, PORT_MOCK) == 0);
    device.derived_info.tdc_channel_count = 2;
    mock_start(&thread, 2);

    KT_CHECK_EQ(katherine_get_tdc_counts(&device, &counts), 0);
    KT_CHECK_EQ(kthread_join(&thread), 0);

    KT_CHECK_EQ(counts.channel[0], expected_count(0));
    KT_CHECK_EQ(counts.channel[1], expected_count(1));
    for (unsigned i = 2; i < KATHERINE_MAX_TDC_CHANNEL_COUNT; ++i) {
        KT_CHECK_EQ(counts.channel[i], 0u);
    }

    katherine_transport_fini(&device.control_transport);
}

// e) One datagram short and the call reports the timeout, writing nothing:
// a part of an answer is not an answer, and a caller that was handed some
// counts and some of its own values could not tell which were which.
static void
test_e_a_short_sequence_times_out(void)
{
    katherine_device_t device;
    katherine_tdc_counts_t counts;
    kthread_t thread;

    for (unsigned i = 0; i < KATHERINE_MAX_TDC_CHANNEL_COUNT; ++i) {
        counts.channel[i] = 0xFFFFFFFFu;
    }

    KT_REQUIRE(device_init(&device, HW_TYPE_GEN2, PORT_DEVICE, PORT_MOCK) == 0);
    mock_start(&thread, KATHERINE_MAX_TDC_CHANNEL_COUNT - 1);

    KT_CHECK_EQ(katherine_get_tdc_counts(&device, &counts), KATHERINE_E_TIMEOUT);
    KT_CHECK_EQ(kthread_join(&thread), 0);

    for (unsigned i = 0; i < KATHERINE_MAX_TDC_CHANNEL_COUNT; ++i) {
        KT_CHECK_EQ(counts.channel[i], 0xFFFFFFFFu);
    }

    katherine_transport_fini(&device.control_transport);
}

// i) The answers firmware 5 splits over USB are still read. The tails it
// interleaves are not command responses and are dropped; the answers
// themselves arrive whole, so the counts come out as they would on a link
// that kept the boundaries. Measured against the readout: UDP and USB both
// report 1 1 0 0 0 after the same frame.
static void
test_i_a_split_sequence_is_still_read(void)
{
    katherine_device_t device;
    katherine_tdc_counts_t counts = {0};
    kthread_t thread;

    KT_REQUIRE(device_init(&device, HW_TYPE_GEN2, PORT_DEVICE, PORT_MOCK) == 0);
    mock_start_split(&thread, KATHERINE_MAX_TDC_CHANNEL_COUNT);

    KT_CHECK_EQ(katherine_get_tdc_counts(&device, &counts), 0);
    KT_CHECK_EQ(kthread_join(&thread), 0);

    for (unsigned i = 0; i < KATHERINE_MAX_TDC_CHANNEL_COUNT; ++i) {
        KT_CHECK_EQ(counts.channel[i], expected_count(i));
    }

    katherine_transport_fini(&device.control_transport);
}

// j) Tails and nothing else, which is a readout answering in a way this
// library cannot read. The tolerance above is bounded so that such a readout
// is reported rather than waited on, and nothing is written.
static void
test_j_nothing_but_short_answers_is_unsupported(void)
{
    katherine_device_t device;
    katherine_tdc_counts_t counts;
    kthread_t thread;

    for (unsigned i = 0; i < KATHERINE_MAX_TDC_CHANNEL_COUNT; ++i) {
        counts.channel[i] = 0xFFFFFFFFu;
    }

    KT_REQUIRE(device_init(&device, HW_TYPE_GEN2, PORT_DEVICE, PORT_MOCK) == 0);
    mock_start_shorts(&thread, KATHERINE_CMD_MAX_STRAY_DISCARDS + 1);

    KT_CHECK_EQ(katherine_get_tdc_counts(&device, &counts), KATHERINE_E_UNSUPPORTED);
    KT_CHECK_EQ(kthread_join(&thread), 0);

    for (unsigned i = 0; i < KATHERINE_MAX_TDC_CHANNEL_COUNT; ++i) {
        KT_CHECK_EQ(counts.channel[i], 0xFFFFFFFFu);
    }

    katherine_transport_fini(&device.control_transport);
}

// k) A first-generation readout cannot report the counts at all: neither of
// its firmwares implements the read, so the command would go out and every
// answer would time out. Refused before anything is sent, which the quiet
// mock proves -- it answers nothing, so a call that reached the wire would
// come back KATHERINE_E_TIMEOUT instead.
static void
test_k_the_first_generation_cannot_report_counts(void)
{
    katherine_device_t device;
    katherine_tdc_counts_t counts;

    KT_REQUIRE(device_init(&device, HW_TYPE_GEN1, PORT_DEVICE_QUIET, PORT_MOCK_QUIET) == 0);

    // The channels are there and configuring them is not refused.
    KT_CHECK(device.derived_info.tdc_channel_count > 0);
    KT_CHECK_EQ(katherine_get_tdc_counts(&device, &counts), KATHERINE_E_UNSUPPORTED);

    katherine_transport_fini(&device.control_transport);
}

#ifdef KATHERINE_TEST_EMULATOR
// f) The emulated readout sends five as well, and nothing after them -- so
// the count is checked against a model of the peer and not only against the
// mock above, which was told how many to send.
static void
test_f_the_emulated_readout_sends_five(void)
{
    katherine_emu_t emu;
    katherine_emu_profile_t profile;
    uint8_t crd[KATHERINE_EMU_CRD_SIZE];
    size_t len;

    katherine_emu_profile_defaults(&profile);
    for (unsigned i = 0; i < KATHERINE_MAX_TDC_CHANNEL_COUNT; ++i) {
        profile.tdc_counts[i] = expected_count(i);
    }
    KT_REQUIRE(katherine_emu_init(&emu, &profile) == 0);

    katherine_cmd_t read = katherine_cmd_create((uint8_t) CMD_TYPE_INTERNAL_TDC_READ_COUNTS);
    KT_REQUIRE(katherine_emu_cmd_in(&emu, read.b, sizeof(read.b)) == 0);

    for (unsigned i = 0; i < KATHERINE_MAX_TDC_CHANNEL_COUNT; ++i) {
        KT_CHECK_EQ(katherine_emu_crd_out(&emu, crd, &len), 0);
        KT_CHECK_EQ(crd[KATHERINE_CMD_OPCODE_BYTE], CMD_TYPE_INTERNAL_TDC_READ_COUNTS);
        KT_CHECK_EQ(load_word(crd), expected_count(i));
    }
    KT_CHECK_EQ(katherine_emu_crd_out(&emu, crd, &len), KATHERINE_E_TIMEOUT);

    // And the command is one it recognizes, which it did not used to be.
    KT_CHECK_EQ(katherine_emu_unknown_cmd_count(&emu), 0);

    katherine_emu_fini(&emu);
}

#endif /* KATHERINE_TEST_EMULATOR */

// g) Every source is named, a value read back off a readout being otherwise
// only a number.
static void
test_g_every_source_is_named(void)
{
    for (unsigned s = 0; s <= KATHERINE_TDC_SOURCE_CHIP_3_FEEDBACK; ++s) {
        KT_CHECK(strcmp(katherine_str_tdc_source((katherine_tdc_source_t) s), "unknown") != 0);
    }

    KT_CHECK_STR_EQ(
        katherine_str_tdc_source((katherine_tdc_source_t) (KATHERINE_TDC_SOURCE_CHIP_3_FEEDBACK + 1)),
        "unknown");
}


// h) A caller that wants the link cleared rather than the counts passes NULL,
// and the whole sequence is still read off so that no answer is left to be
// mistaken for the next command's.
static void
test_h_a_null_array_discards_the_counts(void)
{
    katherine_device_t device;
    kthread_t thread;

    KT_REQUIRE(device_init(&device, HW_TYPE_GEN2, PORT_DEVICE, PORT_MOCK) == 0);
    mock_start(&thread, KATHERINE_MAX_TDC_CHANNEL_COUNT);

    KT_CHECK_EQ(katherine_get_tdc_counts(&device, NULL), 0);

    KT_CHECK_EQ(kthread_join(&thread), 0);
    KT_CHECK_EQ(g_captured[0][KATHERINE_CMD_OPCODE_BYTE], 0x33);

    // Every answer was taken off the link. One left behind would be handed
    // to whichever command came next, so the link has to be empty here.
    uint8_t leftover[KATHERINE_CMD_CRD_SIZE];
    size_t count = sizeof(leftover);
    KT_CHECK_EQ(katherine_transport_recv_crd(&device.control_transport, leftover, &count), KATHERINE_E_TIMEOUT);

    katherine_transport_fini(&device.control_transport);
}

int
main(void)
{
    const int res = mocks_init();
    if (res != 0) {
        printf("1..0 # SKIP cannot bind the loopback endpoints: %s\n", katherine_strerror(res));
        return 77;
    }

    KT_RUN(test_a_the_second_generation_word);
    KT_RUN(test_b_the_first_generation_word);
    KT_RUN(test_c_channels_the_unit_lacks_take_their_defaults);
    KT_RUN(test_d_five_counts_complete_the_sequence);
    KT_RUN(test_e_a_short_sequence_times_out);
#ifdef KATHERINE_TEST_EMULATOR
    KT_RUN(test_f_the_emulated_readout_sends_five);
#endif
    KT_RUN(test_g_every_source_is_named);
    KT_RUN(test_h_a_null_array_discards_the_counts);
    KT_RUN(test_i_a_split_sequence_is_still_read);
    KT_RUN(test_j_nothing_but_short_answers_is_unsupported);
    KT_RUN(test_k_the_first_generation_cannot_report_counts);

    mocks_fini();
    return kt_summary();
}
