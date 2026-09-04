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
#include <string.h>
#include <time.h>

#include <katherine/katherine.h>

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
    katherine_coord_t v = {12, 200};
    char buf[64];
    int n = katherine_coord_snprint(buf, sizeof(buf), &v);
    CHECK_GOLDEN(n, buf, "coord{x: 12, y: 200}");
}

static void
test_px_f_toa_tot(void)
{
    katherine_px_f_toa_tot_t v = {{1, 2}, 123456789012ULL, 300};
    char buf[128];
    int n = katherine_px_f_toa_tot_snprint(buf, sizeof(buf), &v);
    CHECK_GOLDEN(n, buf, "px_f_toa_tot{coord: coord{x: 1, y: 2}, timestamp: 123456789012, tot: 300}");
}

static void
test_px_toa_tot(void)
{
    katherine_px_toa_tot_t v = {{3, 4}, 999999999999ULL, 9, 65000};
    char buf[128];
    int n = katherine_px_toa_tot_snprint(buf, sizeof(buf), &v);
    CHECK_GOLDEN(n, buf, "px_toa_tot{coord: coord{x: 3, y: 4}, timestamp: 999999999999, hit_count: 9, tot: 65000}");
}

static void
test_px_f_toa_only(void)
{
    katherine_px_f_toa_only_t v = {{5, 6}, 42};
    char buf[128];
    int n = katherine_px_f_toa_only_snprint(buf, sizeof(buf), &v);
    CHECK_GOLDEN(n, buf, "px_f_toa_only{coord: coord{x: 5, y: 6}, timestamp: 42}");
}

static void
test_px_toa_only(void)
{
    katherine_px_toa_only_t v = {{7, 8}, 84, 3};
    char buf[128];
    int n = katherine_px_toa_only_snprint(buf, sizeof(buf), &v);
    CHECK_GOLDEN(n, buf, "px_toa_only{coord: coord{x: 7, y: 8}, timestamp: 84, hit_count: 3}");
}

static void
test_px_f_event_count_itot(void)
{
    katherine_px_f_event_count_itot_t v = {{9, 10}, 11, 222};
    char buf[128];
    int n = katherine_px_f_event_count_itot_snprint(buf, sizeof(buf), &v);
    CHECK_GOLDEN(n, buf, "px_f_event_count_itot{coord: coord{x: 9, y: 10}, event_count: 11, integral_tot: 222}");
}

static void
test_px_event_count_itot(void)
{
    katherine_px_event_count_itot_t v = {{11, 12}, 5, 33, 444};
    char buf[128];
    int n = katherine_px_event_count_itot_snprint(buf, sizeof(buf), &v);
    CHECK_GOLDEN(n, buf, "px_event_count_itot{coord: coord{x: 11, y: 12}, hit_count: 5, event_count: 33, integral_tot: 444}");
}

// Both bool states, across the two triggers used again in test_config().
static katherine_trigger_t g_start_trigger = {true, 3, false};
static katherine_trigger_t g_stop_trigger  = {false, 0, true};

static void
test_trigger(void)
{
    char buf[128];

    int n1 = katherine_trigger_snprint(buf, sizeof(buf), &g_start_trigger);
    CHECK_GOLDEN(n1, buf, "trigger{enabled: true, channel: 3, use_falling_edge: false}");

    int n2 = katherine_trigger_snprint(buf, sizeof(buf), &g_stop_trigger);
    CHECK_GOLDEN(n2, buf, "trigger{enabled: false, channel: 0, use_falling_edge: true}");
}

// The approved example from the design: exact format, str_bool in place of
// the ternaries the pre-repr dump_config() used.
static katherine_test_pulse_config_t g_tp_config = {true, false, false, 100, 65, 0};

static void
test_test_pulse_config(void)
{
    char buf[160];
    int n = katherine_test_pulse_config_snprint(buf, sizeof(buf), &g_tp_config);
    CHECK_GOLDEN(n, buf, "test_pulse_config{enabled: true, digital_only: false, external: false, count: 100, period: 65, phase: 0}");
}

// Same values python/tests/smoke.py's build_config() uses, so this golden
// string doubles as a cross-check of that fixture if it is ever repr'd.
static void
fill_dacs(katherine_dacs_t *d)
{
    d->named.Ibias_Preamp_ON   = 128;
    d->named.Ibias_Preamp_OFF  = 8;
    d->named.VPReamp_NCAS      = 128;
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

static const char *DACS_GOLDEN = "dacs{Ibias_Preamp_ON: 128, Ibias_Preamp_OFF: 8, VPReamp_NCAS: 128, Ibias_Ikrum: 15, "
                                 "Vfbk: 164, Vthreshold_fine: 476, Vthreshold_coarse: 8, Ibias_DiscS1_ON: 100, "
                                 "Ibias_DiscS1_OFF: 8, Ibias_DiscS2_ON: 128, Ibias_DiscS2_OFF: 8, Ibias_PixelDAC: 128, "
                                 "Ibias_TPbufferIn: 128, Ibias_TPbufferOut: 128, VTP_coarse: 128, VTP_fine: 256, "
                                 "Ibias_CP_PLL: 128, PLL_Vcntrl: 128}";

static void
test_dacs(void)
{
    katherine_dacs_t d;
    fill_dacs(&d);

    char buf[512];
    int n = katherine_dacs_snprint(buf, sizeof(buf), &d);
    CHECK_GOLDEN(n, buf, DACS_GOLDEN);
}

// Two nonzero words, chosen so the fold is hand-checkable: pair (words[0],
// words[1]) folds to (words[0] << 32) | words[1], every other pair is zero,
// so the whole-array fold is exactly that one nonzero pair.
static void
fill_px_config(katherine_px_config_t *p)
{
    memset(p, 0, sizeof(*p));
    p->words[0] = 0x11111111u;
    p->words[1] = 0x22222222u;
}

static const char *PX_CONFIG_GOLDEN = "px_config{words: 16384, xor64: 0x1111111122222222}";

static void
test_px_config(void)
{
    katherine_px_config_t p;
    fill_px_config(&p);

    char buf[128];
    int n = katherine_px_config_snprint(buf, sizeof(buf), &p);
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
    v.md_buffer_size             = 4096;
    v.pixel_buffer_size          = 8192;

    char buf[512];
    int n = katherine_acquisition_snprint(buf, sizeof(buf), &v);
    CHECK_GOLDEN(n, buf,
        "acquisition{state: succeeded, readout_mode: data_driven, px_mode: only_toa, aborted: true, "
        "requested_frames: 5, completed_frames: 5, dropped_measurement_data: 2, truncated_measurement_data: 1, "
        "md_buffer_size: 4096, pixel_buffer_size: 8192}");
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
#define UDP_TEST_PORT1 43601
#define UDP_TEST_PORT2 43602
#define UDP_TEST_PORT3 43603
#define UDP_TEST_PORT4 43604

// Pinned and in strict mode, with a nonzero stray count: all three of the
// correlation fields rendered away from the value a fresh session has, so
// that a stringifier reading the wrong one shows up here.
static void
test_udp(void)
{
    katherine_udp_t u;
    KT_REQUIRE(katherine_udp_init_bound(&u, UDP_HOST, UDP_TEST_PORT1, UDP_HOST, UDP_TEST_PORT2, 100) == 0);
    katherine_udp_pin_remote(&u);
    katherine_udp_set_strict_ack(&u, true);
    u.stray_command_responses = 3;

    char buf[192];
    int n = katherine_udp_snprint(buf, sizeof(buf), &u);
    CHECK_GOLDEN(n, buf,
        "udp{local: 127.0.0.1:43601, remote: 127.0.0.1:43602, pinned: true, strict_ack: true, "
        "stray_command_responses: 3, last_os_error: 0}");

    katherine_udp_fini(&u);
}

// Unpinned and tolerant, complementing test_udp()'s "true" states with the
// defaults a fresh session carries.
static void
test_device(void)
{
    katherine_device_t dev;
    // Zeroed whole: this device is built by hand rather than by
    // katherine_device_init(), and katherine_device_t carries fields beyond
    // the two sessions -- the borrowed acquisition among them, which
    // katherine_device_fini() acts on.
    memset(&dev, 0, sizeof(dev));
    KT_REQUIRE(katherine_udp_init_bound(&dev.control_socket, UDP_HOST, UDP_TEST_PORT3, UDP_HOST, UDP_TEST_PORT4, 100) == 0);
    KT_REQUIRE(katherine_udp_init_bound(&dev.data_socket, UDP_HOST, UDP_TEST_PORT4, UDP_HOST, UDP_TEST_PORT3, 100) == 0);

    char buf[384];
    int n = katherine_device_snprint(buf, sizeof(buf), &dev);
    CHECK_GOLDEN(n, buf,
        "device{control_socket: udp{local: 127.0.0.1:43603, remote: 127.0.0.1:43604, pinned: false, "
        "strict_ack: false, stray_command_responses: 0, last_os_error: 0}, "
        "data_socket: udp{local: 127.0.0.1:43604, remote: 127.0.0.1:43603, pinned: false, "
        "strict_ack: false, stray_command_responses: 0, last_os_error: 0}}");

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

static const char *CONFIG_GOLDEN = "config{pixel_config: px_config{words: 16384, xor64: 0x1111111122222222}, "
                                   "bias_id: 7, acq_time: 400000, no_frames: 1, bias: 230, "
                                   "start_trigger: trigger{enabled: true, channel: 3, use_falling_edge: false}, "
                                   "delayed_start: false, "
                                   "stop_trigger: trigger{enabled: false, channel: 0, use_falling_edge: true}, "
                                   "gray_disable: true, polarity_holes: false, phase: phase_1, freq: freq_40, "
                                   "dacs: dacs{Ibias_Preamp_ON: 128, Ibias_Preamp_OFF: 8, VPReamp_NCAS: 128, "
                                   "Ibias_Ikrum: 15, Vfbk: 164, Vthreshold_fine: 476, Vthreshold_coarse: 8, "
                                   "Ibias_DiscS1_ON: 100, Ibias_DiscS1_OFF: 8, Ibias_DiscS2_ON: 128, "
                                   "Ibias_DiscS2_OFF: 8, Ibias_PixelDAC: 128, Ibias_TPbufferIn: 128, "
                                   "Ibias_TPbufferOut: 128, VTP_coarse: 128, VTP_fine: 256, Ibias_CP_PLL: 128, "
                                   "PLL_Vcntrl: 128}, "
                                   "test_pulse_config: test_pulse_config{enabled: true, digital_only: false, "
                                   "external: false, count: 100, period: 65, phase: 0}}";

static void
build_config(katherine_config_t *c)
{
    memset(c, 0, sizeof(*c));

    fill_px_config(&c->pixel_config);
    c->bias_id        = 7;
    c->acq_time       = 400000.0;
    c->no_frames      = 1;
    c->bias           = 230.0f;
    c->start_trigger  = g_start_trigger;
    c->delayed_start  = false;
    c->stop_trigger   = g_stop_trigger;
    c->gray_disable   = true;
    c->polarity_holes = false;
    c->phase          = KATHERINE_TPX3_PHASE_1;
    c->freq           = KATHERINE_TPX3_FREQ_40_MHZ;
    fill_dacs(&c->dacs);
    c->test_pulse_config = g_tp_config;
}

static void
test_config(void)
{
    katherine_config_t c;
    build_config(&c);

    char buf[2048];
    int n = katherine_config_snprint(buf, sizeof(buf), &c);
    CHECK_GOLDEN(n, buf, CONFIG_GOLDEN);
}

static void
test_config_nests_dacs_verbatim(void)
{
    katherine_config_t c;
    build_config(&c);

    char config_buf[2048];
    katherine_config_snprint(config_buf, sizeof(config_buf), &c);

    char dacs_buf[512];
    katherine_dacs_snprint(dacs_buf, sizeof(dacs_buf), &c.dacs);

    KT_CHECK(strstr(config_buf, dacs_buf) != NULL);
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
    int n = katherine_test_pulse_config_snprint(clipped, sizeof(clipped), &g_tp_config);

    const char *full = "test_pulse_config{enabled: true, digital_only: false, external: false, count: 100, period: 65, phase: 0}";
    KT_CHECK_EQ(n, (long long) strlen(full));            /* would-be length, not the truncated one */
    KT_CHECK_EQ(strlen(clipped), sizeof(clipped) - 1);   /* buffer filled up to the NUL */
    KT_CHECK_MEM_EQ(clipped, full, sizeof(clipped) - 1); /* and what got written matches the prefix */
}

static void
test_null_sizing_call(void)
{
    int n = katherine_test_pulse_config_snprint(NULL, 0, &g_tp_config);

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
    KT_RUN(test_px_config);
    KT_RUN(test_frame_info_time);
    KT_RUN(test_frame_info);
    KT_RUN(test_acquisition);
    KT_RUN(test_readout_status);
    KT_RUN(test_comm_status);
    KT_RUN(test_udp);
    KT_RUN(test_device);
    KT_RUN(test_str_enums);
    KT_RUN(test_config);
    KT_RUN(test_config_nests_dacs_verbatim);
    KT_RUN(test_truncation);
    KT_RUN(test_null_sizing_call);

    return kt_summary();
}
