/**
 * \file
 * \brief Golden-string tests for katherine_*_snprint() / katherine_str_*().
 *
 * Every covered type gets one hand-computed golden string, filled with
 * distinctive values that exercise both bool states and at least two enum
 * values across the suite. repr.c is plain, portable C -- no sockets beyond
 * the same loopback, high-port pattern c/tests/test_udp_pinning.c already
 * uses for katherine_udp_t (never the fixed 1555/1556 device ports) -- so
 * this test is registered unconditionally, like test_bitfields.c.
 *
 * \author Petr Mánek
 * \date 24.8.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <katherine/katherine.h>

#include "transport/udp.h"

#include "kports.h"
#include "device/private.h"
#include "ktest.h"

// Requires the snprint call to have rendered exactly `expected`: the return
// value (the would-be length, excluding the NUL) equals strlen(expected),
// and the buffer holds those bytes plus the terminating NUL.
#define CHECK_GOLDEN(n, buf, expected) \
    do { \
        KT_CHECK_EQ((n), (long long) strlen((expected))); \
        KT_CHECK_MEM_EQ((buf), (expected), strlen((expected)) + 1); \
    } while (0)

// ------------------------------------------------------------------
// Leaf types.

static void
test_coord(void)
{
    katherine_tpx3_coord_t v = {12, 200};
    char buf[64];
    int n = katherine_tpx3_coord_snprint(buf, sizeof(buf), &v);
    CHECK_GOLDEN(n, buf, "coord{x: 12, y: 200}");
}

static void
test_px_f_toa_tot(void)
{
    katherine_tpx3_px_f_toa_tot_t v = {.chip = 0, .coord = {1, 2}, .timestamp = 123456789012ULL, .tot = 300};
    char buf[128];
    int n = katherine_tpx3_px_f_toa_tot_snprint(buf, sizeof(buf), &v);
    CHECK_GOLDEN(n, buf, "px_f_toa_tot{chip: 0, coord: coord{x: 1, y: 2}, timestamp: 123456789012, tot: 300}");
}

static void
test_px_toa_tot(void)
{
    katherine_tpx3_px_toa_tot_t v = {
        .chip = 1, .coord = {3, 4}, .timestamp = 999999999999ULL, .hit_count = 9, .tot = 65000};
    char buf[128];
    int n = katherine_tpx3_px_toa_tot_snprint(buf, sizeof(buf), &v);
    CHECK_GOLDEN(n, buf, "px_toa_tot{chip: 1, coord: coord{x: 3, y: 4}, timestamp: 999999999999, hit_count: 9, tot: 65000}");
}

static void
test_px_f_toa_only(void)
{
    katherine_tpx3_px_f_toa_only_t v = {.chip = 2, .coord = {5, 6}, .timestamp = 42};
    char buf[128];
    int n = katherine_tpx3_px_f_toa_only_snprint(buf, sizeof(buf), &v);
    CHECK_GOLDEN(n, buf, "px_f_toa_only{chip: 2, coord: coord{x: 5, y: 6}, timestamp: 42}");
}

static void
test_px_toa_only(void)
{
    katherine_tpx3_px_toa_only_t v = {.chip = 3, .coord = {7, 8}, .timestamp = 84, .hit_count = 3};
    char buf[128];
    int n = katherine_tpx3_px_toa_only_snprint(buf, sizeof(buf), &v);
    CHECK_GOLDEN(n, buf, "px_toa_only{chip: 3, coord: coord{x: 7, y: 8}, timestamp: 84, hit_count: 3}");
}

static void
test_px_f_event_count_itot(void)
{
    katherine_tpx3_px_f_event_count_itot_t v = {
        .chip = 0, .coord = {9, 10}, .event_count = 11, .integral_tot = 222};
    char buf[128];
    int n = katherine_tpx3_px_f_event_count_itot_snprint(buf, sizeof(buf), &v);
    CHECK_GOLDEN(n, buf, "px_f_event_count_itot{chip: 0, coord: coord{x: 9, y: 10}, event_count: 11, integral_tot: 222}");
}

static void
test_px_event_count_itot(void)
{
    katherine_tpx3_px_event_count_itot_t v = {
        .chip = 7, .coord = {11, 12}, .hit_count = 5, .event_count = 33, .integral_tot = 444};
    char buf[128];
    int n = katherine_tpx3_px_event_count_itot_snprint(buf, sizeof(buf), &v);
    CHECK_GOLDEN(n, buf, "px_event_count_itot{chip: 7, coord: coord{x: 11, y: 12}, hit_count: 5, event_count: 33, integral_tot: 444}");
}

// Both bool states, across the two triggers used again in test_config().
static katherine_trigger_t g_start_trigger = {
    .use_trigger = true, .channel = 3, .edge = KATHERINE_TRIGGER_EDGE_RISING, .delayed = false, .delay_seconds = 0.0};
static katherine_trigger_t g_stop_trigger = {
    .use_trigger = false, .channel = 0, .edge = KATHERINE_TRIGGER_EDGE_FALLING, .delayed = false, .delay_seconds = 0.0};

static void
test_trigger(void)
{
    char buf[128];

    int n1 = katherine_trigger_snprint(buf, sizeof(buf), &g_start_trigger);
    CHECK_GOLDEN(n1, buf, "trigger{use_trigger: true, channel: 3, edge: rising, delayed: false, delay_seconds: 0}");

    int n2 = katherine_trigger_snprint(buf, sizeof(buf), &g_stop_trigger);
    CHECK_GOLDEN(n2, buf, "trigger{use_trigger: false, channel: 0, edge: falling, delayed: false, delay_seconds: 0}");
}

// The approved example from the design: exact format, str_bool in place of
// the ternaries the pre-repr dump_config() used.
static katherine_tpx3_test_pulse_config_t g_tp_config = {true, false, false, 100, 65, 0};

static void
test_test_pulse_config(void)
{
    char buf[160];
    int n = katherine_tpx3_test_pulse_config_snprint(buf, sizeof(buf), &g_tp_config);
    CHECK_GOLDEN(n, buf, "test_pulse_config{enabled: true, digital_only: false, external: false, count: 100, period: 65, phase: 0}");
}

// Same values python/tests/smoke.py's build_config() uses, so this golden
// string doubles as a cross-check of that fixture if it is ever repr'd.
static void
fill_dacs(katherine_tpx3_dacs_t *d)
{
    d->named.Ibias_Preamp_ON   = 128;
    d->named.Ibias_Preamp_OFF  = 8;
    d->named.Vpreamp_NCAS      = 128;
    d->named.Ibias_Ikrum       = 15;
    d->named.Vfbk              = 164;
    d->named.Vthreshold_fine   = 476;
    d->named.Vthreshold_coarse = 8;
    d->named.Ibias_DiscS1_ON   = 100;
    d->named.Ibias_DiscS1_OFF  = 8;
    d->named.Ibias_DiscS2_ON   = 128;
    d->named.Ibias_DiscS2_OFF  = 8;
    d->named.Ibias_PixelDAC    = 128;
    d->named.Ibias_TPbufferIn  = 128;
    d->named.Ibias_TPbufferOut = 128;
    d->named.VTP_coarse        = 128;
    d->named.VTP_fine          = 256;
    d->named.Ibias_CP_PLL      = 128;
    d->named.PLL_Vcntrl        = 128;
}

static const char *DACS_GOLDEN = "dacs{Ibias_Preamp_ON: 128, Ibias_Preamp_OFF: 8, Vpreamp_NCAS: 128, Ibias_Ikrum: 15, "
                                 "Vfbk: 164, Vthreshold_fine: 476, Vthreshold_coarse: 8, Ibias_DiscS1_ON: 100, "
                                 "Ibias_DiscS1_OFF: 8, Ibias_DiscS2_ON: 128, Ibias_DiscS2_OFF: 8, Ibias_PixelDAC: 128, "
                                 "Ibias_TPbufferIn: 128, Ibias_TPbufferOut: 128, VTP_coarse: 128, VTP_fine: 256, "
                                 "Ibias_CP_PLL: 128, PLL_Vcntrl: 128}";

static void
test_dacs(void)
{
    katherine_tpx3_dacs_t d;
    fill_dacs(&d);

    char buf[512];
    int n = katherine_tpx3_dacs_snprint(buf, sizeof(buf), &d);
    CHECK_GOLDEN(n, buf, DACS_GOLDEN);
}

// A reading per monitor, ascending by a sixteenth of a volt, so a value
// printed under the wrong name is visible in the golden string.
static void
fill_dac_voltages(katherine_tpx3_dac_voltages_t *v)
{
    for (unsigned monitor = 0; monitor < KATHERINE_TPX3_DAC_MONITOR_COUNT; ++monitor) {
        v->array[monitor] = 0.0625f * (float) monitor;
    }
}

static const char *DAC_VOLTAGES_GOLDEN = "dac_voltages{Ibias_Preamp_ON: 0.0000, Ibias_Preamp_OFF: 0.0625, Vpreamp_NCAS: 0.1250, "
                                         "Ibias_Ikrum: 0.1875, Vfbk: 0.2500, Vthreshold_fine: 0.3125, Vthreshold_coarse: 0.3750, "
                                         "Ibias_DiscS1_ON: 0.4375, Ibias_DiscS1_OFF: 0.5000, Ibias_DiscS2_ON: 0.5625, "
                                         "Ibias_DiscS2_OFF: 0.6250, Ibias_PixelDAC: 0.6875, Ibias_TPbufferIn: 0.7500, "
                                         "Ibias_TPbufferOut: 0.8125, VTP_coarse: 0.8750, VTP_fine: 0.9375, Ibias_CP_PLL: 1.0000, "
                                         "PLL_Vcntrl: 1.0625, BandGap output: 1.1250, BandGap_Temp: 1.1875, Ibias_dac: 1.2500, "
                                         "Ibias_dac_cas: 1.3125}";

static void
test_dac_voltages(void)
{
    katherine_tpx3_dac_voltages_t v;
    fill_dac_voltages(&v);

    char buf[768];
    int n = katherine_tpx3_dac_voltages_snprint(buf, sizeof(buf), &v);
    CHECK_GOLDEN(n, buf, DAC_VOLTAGES_GOLDEN);
}

// One timed channel and one the readout does not time, so the golden string
// shows both shapes: the counts are rendered only where they were measured.
static void
fill_gpio_monitor(katherine_gpio_monitor_t *v)
{
    memset(v, 0, sizeof(*v));

    v->channel[0].level      = true;
    v->channel[0].signal     = KATHERINE_GPIO_MONITOR_SIGNAL_PERIODIC;
    v->channel[0].high_ticks = 300000;
    v->channel[0].low_ticks  = 200000;

    v->channel[1].signal = KATHERINE_GPIO_MONITOR_SIGNAL_CONSTANT;

    // Counts left over on a channel that was never timed must not be
    // rendered, which is what the third channel checks.
    v->channel[2].high_ticks = 1234;
}

static const char *GPIO_STATE_GOLDEN = "gpio_monitor{channel0: gpio_channel_monitor{level: 1, signal: periodic, high_ticks: 300000, "
                                       "low_ticks: 200000, frequency: 200.0, duty_cycle: 0.6000}, "
                                       "channel1: gpio_channel_monitor{level: 0, signal: constant}, "
                                       "channel2: gpio_channel_monitor{level: 0, signal: unknown}, "
                                       "channel3: gpio_channel_monitor{level: 0, signal: unknown}, "
                                       "channel4: gpio_channel_monitor{level: 0, signal: unknown}, "
                                       "channel5: gpio_channel_monitor{level: 0, signal: unknown}, "
                                       "channel6: gpio_channel_monitor{level: 0, signal: unknown}, "
                                       "channel7: gpio_channel_monitor{level: 0, signal: unknown}}";

static void
test_gpio_monitor(void)
{
    katherine_gpio_monitor_t v;
    fill_gpio_monitor(&v);

    char buf[1024];
    int n = katherine_gpio_monitor_snprint(buf, sizeof(buf), &v);
    CHECK_GOLDEN(n, buf, GPIO_STATE_GOLDEN);
}

// Two nonzero words, chosen so the fold is hand-checkable: pair (words[0],
// words[1]) folds to (words[0] << 32) | words[1], every other pair is zero,
// so the whole-array fold is exactly that one nonzero pair.
static void
fill_px_config(katherine_tpx3_px_config_t *p)
{
    memset(p, 0, sizeof(*p));
    p->words[0] = 0x11111111u;
    p->words[1] = 0x22222222u;
}

static const char *PX_CONFIG_GOLDEN = "px_config{words: 16384, xor64: 0x1111111122222222}";

static void
test_px_config(void)
{
    katherine_tpx3_px_config_t p;
    fill_px_config(&p);

    char buf[128];
    int n = katherine_tpx3_px_config_snprint(buf, sizeof(buf), &p);
    CHECK_GOLDEN(n, buf, PX_CONFIG_GOLDEN);
}

static void
test_frame_info_time(void)
{
    katherine_frame_info_time_t v;
    v.b.lsb = 1;
    v.b.msb = 2;

    char buf[128];
    int n = katherine_frame_info_time_snprint(buf, sizeof(buf), &v);
    CHECK_GOLDEN(n, buf, "frame_info_time{d: 8589934593, msb: 2, lsb: 1}");
}

static void
test_frame_info(void)
{
    katherine_frame_info_t v;
    memset(&v, 0, sizeof(v));

    v.received_pixels     = 40;
    v.sent_pixels         = 40;
    v.lost_pixels         = 3;
    v.start_time.b.lsb    = 1000;
    v.end_time.b.lsb      = 17000;
    v.start_time_observed = (time_t) 1700000000;
    v.end_time_observed   = (time_t) 1700000001;
    v.completed           = true;

    char buf[512];
    int n = katherine_frame_info_snprint(buf, sizeof(buf), &v);
    CHECK_GOLDEN(n, buf,
        "frame_info{received_pixels: 40, sent_pixels: 40, lost_pixels: 3, "
        "start_time: frame_info_time{d: 1000, msb: 0, lsb: 1000}, "
        "end_time: frame_info_time{d: 17000, msb: 0, lsb: 17000}, "
        "start_time_observed: 1700000000, end_time_observed: 1700000001, completed: true}");
}

// px_mode/readout_mode are 'char' fields storing enum values; only_toa and
// data_driven give this a second and third enum token beyond phase_1/freq_40
// in test_config() and succeeded in test_str_acquisition_status_tokens().
static void
test_acquisition(void)
{
    katherine_acquisition_t v;
    memset(&v, 0, sizeof(v));

    v.state                      = KATHERINE_ACQUISITION_STATE_SUCCEEDED;
    v.readout_mode               = KATHERINE_TPX3_READOUT_DATA_DRIVEN;
    v.px_mode                    = KATHERINE_TPX3_PX_ONLY_TOA;
    v.aborted                    = true;
    v.requested_frames           = 5;
    v.completed_frames           = 5;
    v.dropped_measurement_data   = 2;
    v.truncated_measurement_data = 1;
    v.received_measurement_data  = 98304;
    v.md_buffer_size             = 4096;
    v.pixel_buffer_size          = 8192;

    char buf[512];
    int n = katherine_acquisition_snprint(buf, sizeof(buf), &v);
    CHECK_GOLDEN(n, buf,
        "acquisition{state: succeeded, readout_mode: data_driven, px_mode: only_toa, aborted: true, "
        "requested_frames: 5, completed_frames: 5, dropped_measurement_data: 2, truncated_measurement_data: 1, "
        "received_measurement_data: 98304, md_buffer_size: 4096, pixel_buffer_size: 8192}");
}

static void
test_readout_status(void)
{
    katherine_readout_status_t v = {1, 2, 12345, 7};
    char buf[128];
    int n = katherine_readout_status_snprint(buf, sizeof(buf), &v);
    CHECK_GOLDEN(n, buf, "readout_status{hw_type: 1, hw_revision: 2, hw_serial_number: 12345, fw_version: 7}");
}

static void
test_comm_status(void)
{
    katherine_comm_status_t v = {0xFF, 123456, true};
    char buf[128];
    int n = katherine_comm_status_snprint(buf, sizeof(buf), &v);
    CHECK_GOLDEN(n, buf, "comm_status{comm_lines_mask: 0xff, data_rate: 123456 Mb/s, chip_count: 1}");
}

// ------------------------------------------------------------------
// Types that own a live resource: uncommon, high loopback ports of their
// own, exactly like c/tests/test_udp_pinning.c -- never the fixed
// 1555/1556 a device session claims, so no global resource is touched and
// no serialization is needed.

#define UDP_HOST       "127.0.0.1"
#define UDP_TEST_PORT1 (KPORTS_REPR + 0)
#define UDP_TEST_PORT2 (KPORTS_REPR + 1)
#define UDP_TEST_PORT3 (KPORTS_REPR + 2)
#define UDP_TEST_PORT4 (KPORTS_REPR + 3)

// Pinned, so the one field a fresh session renders differently is rendered
// away from its default and a stringifier reading the wrong one shows up.
static void
test_udp(void)
{
    katherine_udp_t u;
    KT_REQUIRE(katherine_udp_init_bound(&u, UDP_HOST, UDP_TEST_PORT1, UDP_HOST, UDP_TEST_PORT2, 100) == 0);
    katherine_udp_pin_remote(&u);

    char buf[192];
    int n = katherine_udp_snprint(buf, sizeof(buf), &u);
    char want[192];
    snprintf(want, sizeof(want), "udp{local: 127.0.0.1:%u, remote: 127.0.0.1:%u, pinned: true, last_os_error: 0}",
        (unsigned) UDP_TEST_PORT1, (unsigned) UDP_TEST_PORT2);
    CHECK_GOLDEN(n, buf, want);

    katherine_udp_fini(&u);
}

// Strict and with a nonzero stray count: both correlation fields rendered
// away from what a freshly opened transport carries. They live here rather
// than on the session because correlation is the protocol's business and
// not the link's.
static void
test_transport(void)
{
    const katherine_udp_config_t config = {
        .local_address  = UDP_HOST,
        .local_port     = UDP_TEST_PORT1,
        .remote_address = UDP_HOST,
        .remote_port    = UDP_TEST_PORT2,
        .timeout_ms     = 100,
    };

    katherine_transport_t t;
    KT_REQUIRE(katherine_transport_init_udp(&t, &config) == 0);
    KT_CHECK_EQ(t.strict_ack, false);
    KT_CHECK_EQ(t.stray_command_responses, 0u);

    katherine_transport_set_strict_ack(&t, true);
    t.stray_command_responses = 3;

    char buf[128];
    int n = katherine_transport_snprint(buf, sizeof(buf), &t);
    CHECK_GOLDEN(n, buf, "transport{kind: udp, strict_ack: true, stray_command_responses: 3}");

    katherine_transport_fini(&t);
}

// Unpinned and tolerant, complementing test_udp()'s "true" states with the
// defaults a fresh session carries.
static void
test_device(void)
{
    katherine_device_t dev;
    // Zeroed whole: this device is built by hand rather than by
    // katherine_device_init(), and katherine_device_t carries fields beyond
    // its control link -- the borrowed acquisition among them, which
    // katherine_device_fini() acts on.
    memset(&dev, 0, sizeof(dev));
    const katherine_udp_config_t control = {
        .local_address  = UDP_HOST,
        .local_port     = UDP_TEST_PORT3,
        .remote_address = UDP_HOST,
        .remote_port    = UDP_TEST_PORT4,
        .timeout_ms     = 100,
    };
    KT_REQUIRE(katherine_transport_init_udp(&dev.control_transport, &control) == 0);
    strcpy(dev.address, "udp://" UDP_HOST);
    katherine_device_state(&dev)->udp_data_port = UDP_TEST_PORT4;

    char buf[384];
    int n = katherine_device_snprint(buf, sizeof(buf), &dev);
    char want[384];
    snprintf(want, sizeof(want),
        "device{address: \"udp://127.0.0.1\", "
        "control_transport: transport{kind: udp, strict_ack: false, stray_command_responses: 0}, "
        "udp_data_port: %u}",
        (unsigned) UDP_TEST_PORT4);
    CHECK_GOLDEN(n, buf, want);

    katherine_device_fini(&dev);
}

// ------------------------------------------------------------------
// katherine_str_*() tokens: unknown-value fallback and a couple of the
// enumerators, on top of the ones already exercised via test_config() and
// test_acquisition().

static void
test_str_enums(void)
{
    KT_CHECK(strcmp(katherine_str_readout_mode(KATHERINE_TPX3_READOUT_SEQUENTIAL), "sequential") == 0);
    KT_CHECK(strcmp(katherine_str_readout_mode(KATHERINE_TPX3_READOUT_DATA_DRIVEN), "data_driven") == 0);
    KT_CHECK(strcmp(katherine_str_readout_mode((katherine_tpx3_readout_mode_t) 99), "unknown") == 0);

    KT_CHECK(strcmp(katherine_str_px_mode(KATHERINE_TPX3_PX_TOA_TOT), "toa_tot") == 0);
    KT_CHECK(strcmp(katherine_str_px_mode(KATHERINE_TPX3_PX_EVENT_COUNT_ITOT), "event_count_itot") == 0);
    KT_CHECK(strcmp(katherine_str_px_mode((katherine_tpx3_px_mode_t) 99), "unknown") == 0);

    KT_CHECK(strcmp(katherine_str_phase(KATHERINE_TPX3_PHASE_1), "phase_1") == 0);
    KT_CHECK(strcmp(katherine_str_phase(KATHERINE_TPX3_PHASE_16), "phase_16") == 0);
    KT_CHECK(strcmp(katherine_str_phase((katherine_tpx3_phase_t) 99), "unknown") == 0);

    KT_CHECK(strcmp(katherine_str_freq(KATHERINE_TPX3_FREQ_40_MHZ), "freq_40") == 0);
    KT_CHECK(strcmp(katherine_str_freq(KATHERINE_TPX3_FREQ_160_MHZ), "freq_160") == 0);
    KT_CHECK(strcmp(katherine_str_freq((katherine_tpx3_freq_t) 99), "unknown") == 0);

    // Updated (2026-08-24) to underscore-separated tokens; see acquisition.c.
    KT_CHECK(strcmp(katherine_str_acquisition_state(KATHERINE_ACQUISITION_STATE_NOT_STARTED), "not_started") == 0);
    KT_CHECK(strcmp(katherine_str_acquisition_state(KATHERINE_ACQUISITION_STATE_TIMED_OUT), "timed_out") == 0);
    KT_CHECK(strcmp(katherine_str_acquisition_state((katherine_acquisition_state_t) 99), "unknown") == 0);

    KT_CHECK(strcmp(katherine_str_bool(true), "true") == 0);
    KT_CHECK(strcmp(katherine_str_bool(false), "false") == 0);
}

// ------------------------------------------------------------------
// Nesting: katherine_config_t composes five nested snprint calls plus its
// own plain fields. This is the whole approved example (test_pulse_config)
// nested inside the whole dacs golden string, inside the whole config
// string, with the pixel matrix rendered as the words/xor64 digest.

static const char *CONFIG_GOLDEN = "tpx3_config{common: config{chip_type: Timepix3, chip_count: 1, "
                                   "frame_duration_seconds: 0.0004, frame_count: 1, bias0: 230, bias1: 12.5, "
                                   "bias2: 0, bias3: 0, "
                                   "start_trigger: trigger{use_trigger: true, channel: 3, edge: rising, delayed: false, delay_seconds: 0}, "
                                   "stop_trigger: trigger{use_trigger: false, channel: 0, edge: falling, delayed: false, delay_seconds: 0}}, "
                                   "readout_mode: data_driven, px_mode: toa_tot, fast_vco_enable: true, "
                                   "clock_phase: phase_1, correct_clock_phase: false, clock_frequency: freq_40, "
                                   "test_pulses: test_pulse_config{enabled: true, digital_only: false, "
                                   "external: false, count: 100, period: 65, phase: 0}, "
                                   "chip0: tpx3_chip_config{polarity: electrons, gray_disable: true, "
                                   "dacs: dacs{Ibias_Preamp_ON: 128, Ibias_Preamp_OFF: 8, Vpreamp_NCAS: 128, "
                                   "Ibias_Ikrum: 15, Vfbk: 164, Vthreshold_fine: 476, Vthreshold_coarse: 8, "
                                   "Ibias_DiscS1_ON: 100, Ibias_DiscS1_OFF: 8, Ibias_DiscS2_ON: 128, "
                                   "Ibias_DiscS2_OFF: 8, Ibias_PixelDAC: 128, Ibias_TPbufferIn: 128, "
                                   "Ibias_TPbufferOut: 128, VTP_coarse: 128, VTP_fine: 256, Ibias_CP_PLL: 128, "
                                   "PLL_Vcntrl: 128}, "
                                   "pixels: px_config{words: 16384, xor64: 0x1111111122222222}}}";


// A structure with a flexible array member cannot be a member of another, so
// one chip's worth of configuration is allocated rather than declared.
#define CONFIG_SIZE_1 (KATHERINE_TPX3_CONFIG_SIZE(1))

static katherine_tpx3_config_t *
alloc_config_1(void)
{
    return (katherine_tpx3_config_t *) calloc(1, CONFIG_SIZE_1);
}

static void
build_config(katherine_tpx3_config_t *c)
{
    memset(c, 0, CONFIG_SIZE_1);

    c->common.chip_type              = KATHERINE_CHIP_TPX3;
    c->common.chip_count             = 1;
    c->common.frame_duration_seconds = 0.0004;
    c->common.frame_count            = 1;
    c->common.bias[0]                = 230.0f;
    c->common.bias[1]                = 12.5f;
    c->common.start_trigger          = g_start_trigger;
    c->common.stop_trigger           = g_stop_trigger;

    c->readout_mode    = KATHERINE_TPX3_READOUT_DATA_DRIVEN;
    c->px_mode         = KATHERINE_TPX3_PX_TOA_TOT;
    c->fast_vco_enable = true;
    c->clock_phase     = KATHERINE_TPX3_PHASE_1;
    c->clock_frequency = KATHERINE_TPX3_FREQ_40_MHZ;
    c->test_pulses     = g_tp_config;

    c->chips[0].gray_disable = true;
    c->chips[0].polarity     = KATHERINE_POLARITY_ELECTRONS;
    fill_dacs(&c->chips[0].dacs);
    fill_px_config(&c->chips[0].pixels);
}

static void
test_config(void)
{
    katherine_tpx3_config_t *c = alloc_config_1();
    KT_REQUIRE(c != NULL);
    build_config(c);

    char buf[2048];
    int n = katherine_tpx3_config_snprint(buf, sizeof(buf), c);
    CHECK_GOLDEN(n, buf, CONFIG_GOLDEN);
    free(c);
}

static void
test_config_nests_dacs_verbatim(void)
{
    katherine_tpx3_config_t *c = alloc_config_1();
    KT_REQUIRE(c != NULL);
    build_config(c);

    char config_buf[2048];
    katherine_tpx3_config_snprint(config_buf, sizeof(config_buf), c);

    char dacs_buf[512];
    katherine_tpx3_dacs_snprint(dacs_buf, sizeof(dacs_buf), &c->chips[0].dacs);

    KT_CHECK(strstr(config_buf, dacs_buf) != NULL);
    free(c);
}

// ------------------------------------------------------------------
// Cross-cutting snprintf semantics, checked once here rather than per type:
// every katherine_*_snprint() shares the same REPR_APPENDF/REPR_NEST
// machinery (repr.c), so a single representative type (test_pulse_config)
// stands in for all of them.

static void
test_truncation(void)
{
    char clipped[10];
    int n = katherine_tpx3_test_pulse_config_snprint(clipped, sizeof(clipped), &g_tp_config);

    const char *full = "test_pulse_config{enabled: true, digital_only: false, external: false, count: 100, period: 65, phase: 0}";
    KT_CHECK_EQ(n, (long long) strlen(full));            /* would-be length, not the truncated one */
    KT_CHECK_EQ(strlen(clipped), sizeof(clipped) - 1);   /* buffer filled up to the NUL */
    KT_CHECK_MEM_EQ(clipped, full, sizeof(clipped) - 1); /* and what got written matches the prefix */
}

// ------------------------------------------------------------------
// And the same contract for every renderer, at every size a caller can pass.
//
// The one above covers one renderer at one capacity. This covers all of
// them from a buffer of nothing up to one with room to spare: the return
// value is the would-be length whatever the capacity, the terminator lands
// at min(cap - 1, that length), and what precedes it is the prefix of the
// full rendering. Each value is zeroed rather than filled -- what the text
// says is the business of the golden cases above, and what is checked here
// is only where it stops.
//
// An adapter per renderer rather than a cast to a common signature: calling
// a function through an incompatible pointer type is undefined, and the
// sanitizer legs CI runs would be within their rights to say so. If a
// renderer is added, tools/debuggers/tags_selftest.py is what enumerates
// them.

typedef int (*repr_renderer_t)(char *buf, size_t cap, const void *value);

#define RENDER_ADAPTER(SHORT, TYPE) \
    static int adapt_##SHORT(char *buf, size_t cap, const void *value) \
    { \
        static const TYPE zero = {0}; \
        (void) value; \
        return katherine_##SHORT(buf, cap, &zero); \
    }

RENDER_ADAPTER(frame_info_time_snprint, katherine_frame_info_time_t)
RENDER_ADAPTER(frame_info_snprint, katherine_frame_info_t)
RENDER_ADAPTER(acquisition_snprint, katherine_acquisition_t)
RENDER_ADAPTER(trigger_snprint, katherine_trigger_t)
RENDER_ADAPTER(tpx3_test_pulse_config_snprint, katherine_tpx3_test_pulse_config_t)
RENDER_ADAPTER(tpx3_dacs_snprint, katherine_tpx3_dacs_t)
RENDER_ADAPTER(tpx3_dac_voltages_snprint, katherine_tpx3_dac_voltages_t)
RENDER_ADAPTER(tpx3_chip_config_snprint, katherine_tpx3_chip_config_t)
RENDER_ADAPTER(config_snprint, katherine_config_t)
RENDER_ADAPTER(tpx3_config_snprint, katherine_tpx3_config_t)
RENDER_ADAPTER(tpx3_config_check_snprint, katherine_tpx3_config_check_t)
RENDER_ADAPTER(device_info_snprint, katherine_device_info_t)
RENDER_ADAPTER(device_derived_info_snprint, katherine_device_derived_info_t)
RENDER_ADAPTER(device_snprint, katherine_device_t)
RENDER_ADAPTER(gpio_channel_monitor_snprint, katherine_gpio_channel_monitor_t)
RENDER_ADAPTER(gpio_monitor_snprint, katherine_gpio_monitor_t)
RENDER_ADAPTER(tpx3_px_config_snprint, katherine_tpx3_px_config_t)
RENDER_ADAPTER(tpx3_coord_snprint, katherine_tpx3_coord_t)
RENDER_ADAPTER(tpx3_px_f_toa_tot_snprint, katherine_tpx3_px_f_toa_tot_t)
RENDER_ADAPTER(tpx3_px_toa_tot_snprint, katherine_tpx3_px_toa_tot_t)
RENDER_ADAPTER(tpx3_px_f_toa_only_snprint, katherine_tpx3_px_f_toa_only_t)
RENDER_ADAPTER(tpx3_px_toa_only_snprint, katherine_tpx3_px_toa_only_t)
RENDER_ADAPTER(tpx3_px_f_event_count_itot_snprint, katherine_tpx3_px_f_event_count_itot_t)
RENDER_ADAPTER(tpx3_px_event_count_itot_snprint, katherine_tpx3_px_event_count_itot_t)
RENDER_ADAPTER(readout_status_snprint, katherine_readout_status_t)
RENDER_ADAPTER(comm_status_snprint, katherine_comm_status_t)
RENDER_ADAPTER(tdc_counts_snprint, katherine_tdc_counts_t)
RENDER_ADAPTER(transport_snprint, katherine_transport_t)

static const struct {
    const char *name;
    repr_renderer_t render;
} REPR_RENDERERS[] = {
    {"katherine_frame_info_time_snprint", adapt_frame_info_time_snprint},
    {"katherine_frame_info_snprint", adapt_frame_info_snprint},
    {"katherine_acquisition_snprint", adapt_acquisition_snprint},
    {"katherine_trigger_snprint", adapt_trigger_snprint},
    {"katherine_tpx3_test_pulse_config_snprint", adapt_tpx3_test_pulse_config_snprint},
    {"katherine_tpx3_dacs_snprint", adapt_tpx3_dacs_snprint},
    {"katherine_tpx3_dac_voltages_snprint", adapt_tpx3_dac_voltages_snprint},
    {"katherine_tpx3_chip_config_snprint", adapt_tpx3_chip_config_snprint},
    {"katherine_config_snprint", adapt_config_snprint},
    {"katherine_tpx3_config_snprint", adapt_tpx3_config_snprint},
    {"katherine_tpx3_config_check_snprint", adapt_tpx3_config_check_snprint},
    {"katherine_device_info_snprint", adapt_device_info_snprint},
    {"katherine_device_derived_info_snprint", adapt_device_derived_info_snprint},
    {"katherine_device_snprint", adapt_device_snprint},
    {"katherine_gpio_channel_monitor_snprint", adapt_gpio_channel_monitor_snprint},
    {"katherine_gpio_monitor_snprint", adapt_gpio_monitor_snprint},
    {"katherine_tpx3_px_config_snprint", adapt_tpx3_px_config_snprint},
    {"katherine_tpx3_coord_snprint", adapt_tpx3_coord_snprint},
    {"katherine_tpx3_px_f_toa_tot_snprint", adapt_tpx3_px_f_toa_tot_snprint},
    {"katherine_tpx3_px_toa_tot_snprint", adapt_tpx3_px_toa_tot_snprint},
    {"katherine_tpx3_px_f_toa_only_snprint", adapt_tpx3_px_f_toa_only_snprint},
    {"katherine_tpx3_px_toa_only_snprint", adapt_tpx3_px_toa_only_snprint},
    {"katherine_tpx3_px_f_event_count_itot_snprint", adapt_tpx3_px_f_event_count_itot_snprint},
    {"katherine_tpx3_px_event_count_itot_snprint", adapt_tpx3_px_event_count_itot_snprint},
    {"katherine_readout_status_snprint", adapt_readout_status_snprint},
    {"katherine_comm_status_snprint", adapt_comm_status_snprint},
    {"katherine_tdc_counts_snprint", adapt_tdc_counts_snprint},
    {"katherine_transport_snprint", adapt_transport_snprint},
};

static void
test_every_renderer_stops_where_it_is_told(void)
{
    char golden[4096];

    for (size_t i = 0; i < sizeof(REPR_RENDERERS) / sizeof(REPR_RENDERERS[0]); ++i) {
        const repr_renderer_t render = REPR_RENDERERS[i].render;

        const int full = render(golden, sizeof(golden), NULL);
        KT_REQUIRE(full > 0 && (size_t) full < sizeof(golden));

        for (size_t cap = 0; cap <= (size_t) full + 2; ++cap) {
            // Exactly cap bytes, so an overrun is one the sanitizer legs see.
            char *buf = (char *) malloc(cap == 0 ? 1 : cap);
            KT_REQUIRE(buf != NULL);

            const int n = render(buf, cap, NULL);
            KT_CHECK_EQ(n, (long long) full);

            if (cap > 0) {
                const size_t end = (cap - 1 < (size_t) full) ? cap - 1 : (size_t) full;
                KT_CHECK_EQ((int) buf[end], 0);
                if (end > 0) KT_CHECK_MEM_EQ(buf, golden, end);
            }

            free(buf);
        }
    }
}

static void
test_null_sizing_call(void)
{
    int n = katherine_tpx3_test_pulse_config_snprint(NULL, 0, &g_tp_config);

    const char *full = "test_pulse_config{enabled: true, digital_only: false, external: false, count: 100, period: 65, phase: 0}";
    KT_CHECK_EQ(n, (long long) strlen(full));
}

// ------------------------------------------------------------------

int
main(void)
{
    KT_RUN(test_coord);
    KT_RUN(test_px_f_toa_tot);
    KT_RUN(test_px_toa_tot);
    KT_RUN(test_px_f_toa_only);
    KT_RUN(test_px_toa_only);
    KT_RUN(test_px_f_event_count_itot);
    KT_RUN(test_px_event_count_itot);
    KT_RUN(test_trigger);
    KT_RUN(test_test_pulse_config);
    KT_RUN(test_dacs);
    KT_RUN(test_dac_voltages);
    KT_RUN(test_gpio_monitor);
    KT_RUN(test_px_config);
    KT_RUN(test_frame_info_time);
    KT_RUN(test_frame_info);
    KT_RUN(test_acquisition);
    KT_RUN(test_readout_status);
    KT_RUN(test_comm_status);
    KT_RUN(test_udp);
    KT_RUN(test_transport);
    KT_RUN(test_device);
    KT_RUN(test_str_enums);
    KT_RUN(test_config);
    KT_RUN(test_config_nests_dacs_verbatim);
    KT_RUN(test_truncation);
    KT_RUN(test_every_renderer_stops_where_it_is_told);
    KT_RUN(test_null_sizing_call);

    return kt_summary();
}
