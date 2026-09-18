// The readout generation decides which measurement-data headers carry pixels,
// and getting it wrong is silent in both directions.
//
// Gen1 sends pixels under header 0x4 and trigger info under 0x2 and 0x3. Gen2
// sends pixels under 0x0 through 0x3, one header per chip, and splits the
// trigger event across 0x4 and 0x6. So a Gen2 stream decoded by the Gen1 map
// loses every pixel -- measured on hw_type 3 / fw_version 5, 152 251 of
// 152 287 words in a three-frame run arrived as 0x0 and not one as 0x4 -- while
// reporting frame boundaries perfectly, and it turns every trigger word into a
// hit with nonsense coordinates.
//
// Drives the real read loop over a localhost UDP socket, as test_issue16 does,
// because the point is the loop the library actually dispatches to rather than
// a hand-called handler. The streams are crafted rather than recorded: a
// recorded one would have to be committed as a fixture, and the counts asserted
// here are exact, which a capture cannot promise across trims.
//
// Copyright (c) 2018 Petr Mánek.
// This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
//
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <katherine/acquisition.h>
#include <katherine/device.h>
#include <katherine/error.h>
#include <katherine/toa.h>
#include <katherine/udp.h>

#include "ktest.h"

#define MD_SIZE     6
#define PORT_DATA   42620
#define PORT_SENDER 42621

static void
make_md(unsigned char *dst, unsigned header, uint64_t payload)
{
    uint64_t w = ((uint64_t) (header & 0xF) << 44) | (payload & ((1ULL << 44) - 1));
    for (int i = 0; i < MD_SIZE; ++i) {
        dst[i] = (unsigned char) (w >> (8 * i));
    }
}

struct stats {
    size_t pixels;
    unsigned chips_seen; ///< bit i set if a hit reported chip i
    int frames_started;
    int frames_ended;
};

static void
pixels_received(void *ctx, const void *px, size_t count)
{
    struct stats *s                 = ctx;
    const katherine_px_toa_tot_t *p = px;

    s->pixels += count;
    for (size_t i = 0; i < count; ++i) {
        s->chips_seen |= 1u << (p[i].chip & 0x1F);
    }
}

static void
frame_started(void *ctx, int frame_idx)
{
    struct stats *s = ctx;
    (void) frame_idx;
    ++s->frames_started;
}

static void
frame_ended(void *ctx, int frame_idx, bool completed, const katherine_frame_info_t *info)
{
    struct stats *s = ctx;
    (void) frame_idx;
    (void) completed;
    (void) info;
    ++s->frames_ended;
}

/**
 * Push one crafted stream through the read loop of a device of the given
 * generation, and report what came out.
 *
 * \param gen Readout generation to decode as
 * \param stream Measurement-data words, already packed
 * \param words Words in stream
 * \param s Out-parameter for what the handlers saw
 * \return Whatever katherine_acquisition_read() returned
 */
static katherine_error_t
run_stream(uint8_t gen, const unsigned char *stream, size_t words, struct stats *s)
{
    memset(s, 0, sizeof(*s));

    katherine_device_t dev;
    memset(&dev, 0, sizeof(dev));

    // The generation normally arrives from the enumeration probe. Declared
    // here instead, which is what a caller with no readout to ask does, and
    // which reaches derived_info by the same single path enumeration uses --
    // hardware type 0x01 is the Gen1 Katherine, 0x03 the Gen2.
    const katherine_device_info_t id = {.hw_type = (gen >= 2) ? 0x03 : 0x01};
    KT_CHECK(katherine_device_declare(&dev, &id) == KATHERINE_E_OK);
    KT_CHECK_EQ(dev.derived_info.gen, gen);

    katherine_error_t res = katherine_udp_init_bound(&dev.data_socket, "127.0.0.1", PORT_DATA, "127.0.0.1", 1, 100);
    KT_CHECK(res == KATHERINE_E_OK);
    if (res != KATHERINE_E_OK) return res;

    katherine_acquisition_t acq;
    memset(&acq, 0, sizeof(acq));
    res = katherine_acquisition_init(&acq, &dev, s, 1024 * MD_SIZE, 65536, 0, 1);
    KT_CHECK(res == KATHERINE_E_OK);
    if (res != KATHERINE_E_OK) {
        katherine_udp_fini(&dev.data_socket);
        return res;
    }

    acq.handlers.pixels_received = pixels_received;
    acq.handlers.frame_started   = frame_started;
    acq.handlers.frame_ended     = frame_ended;

    // Stands in for katherine_acquisition_begin(), which needs hardware.
    acq.state                         = KATHERINE_ACQUISITION_STATE_RUNNING;
    acq.toa_coarse_tick_to_fine_shift = katherine_tpx3_toa_coarse_tick_to_fine_shift(KATHERINE_TPX3_FREQ_40_MHZ);
    acq.last_toa_offset               = katherine_tpx3_toa_coarse_tick_to_fine_ticks(KATHERINE_TPX3_FREQ_40_MHZ);
    acq.px_mode                       = KATHERINE_TPX3_PX_TOA_TOT;
    acq.fast_vco_enabled              = false;
    acq.decode_data                   = true;
    acq.requested_frames              = 1;
    acq.acq_start_time                = time(NULL);

    katherine_udp_t sender;
    KT_CHECK(katherine_udp_init_bound(&sender, "127.0.0.1", PORT_SENDER, "127.0.0.1", PORT_DATA, 0)
        == KATHERINE_E_OK);
    KT_CHECK(katherine_udp_send_exact(&sender, stream, words * MD_SIZE) == KATHERINE_E_OK);
    katherine_udp_fini(&sender);

    res = katherine_acquisition_read(&acq);

    const size_t dropped = acq.dropped_measurement_data;
    katherine_acquisition_fini(&acq);
    katherine_udp_fini(&dev.data_socket);

    // Folded into chips_seen's spare high bit rather than widening the struct:
    // the tests below care only whether anything was dropped.
    if (dropped != 0) s->chips_seen |= 1u << 31;
    return res;
}

// Four pixels, one per Gen2 chip header, wrapped in a frame.
static size_t
build_gen2_pixels(unsigned char *out)
{
    size_t n = 0;
    make_md(out + (n++) * MD_SIZE, 0x7, 0);
    for (unsigned chip = 0; chip <= 3; ++chip) {
        make_md(out + (n++) * MD_SIZE, chip, 0x123456 + chip);
    }
    make_md(out + (n++) * MD_SIZE, 0xC, 4);
    return n;
}

static void
test_gen2_decodes_all_four_chip_headers(void)
{
    unsigned char stream[8 * MD_SIZE];
    const size_t n = build_gen2_pixels(stream);

    struct stats s;
    (void) run_stream(2, stream, n, &s);

    KT_CHECK_EQ(s.pixels, 4u);
    // One hit from each of chips 0..3, and nothing dropped.
    KT_CHECK_EQ(s.chips_seen, 0xFu);
    KT_CHECK_EQ(s.frames_started, 1);
}

static void
test_gen1_loses_every_gen2_pixel(void)
{
    unsigned char stream[8 * MD_SIZE];
    const size_t n = build_gen2_pixels(stream);

    struct stats s;
    (void) run_stream(1, stream, n, &s);

    // The defect this commit fixes, asserted rather than described: the Gen1
    // map sees no pixels in a Gen2 stream, yet opens and closes the frame.
    KT_CHECK_EQ(s.pixels, 0u);
    KT_CHECK_EQ(s.frames_started, 1);
    KT_CHECK(s.chips_seen & (1u << 31)); // and counts them as dropped
}

static void
test_gen2_does_not_mistake_a_trigger_for_a_pixel(void)
{
    // 0x4 is the Gen1 pixel header and the Gen2 trigger's low half; 0x6 is
    // unused on Gen1 and the high half on Gen2.
    unsigned char stream[8 * MD_SIZE];
    size_t n = 0;
    make_md(stream + (n++) * MD_SIZE, 0x7, 0);
    make_md(stream + (n++) * MD_SIZE, 0x4, 0xABCDEF);
    make_md(stream + (n++) * MD_SIZE, 0x6, 0x123456);
    make_md(stream + (n++) * MD_SIZE, 0xC, 0);

    struct stats s;
    (void) run_stream(2, stream, n, &s);
    KT_CHECK_EQ(s.pixels, 0u);

    // The same stream on Gen1: 0x4 is a pixel there, which is exactly why
    // decoding a Gen2 trigger with the Gen1 map fabricates a hit.
    (void) run_stream(1, stream, n, &s);
    KT_CHECK_EQ(s.pixels, 1u);
}

static void
test_gen2_triggers_between_pixels_disturb_nothing(void)
{
    // Triggers among the pixels rather than alone in the stream: every trigger
    // reaches the handler that discards it, and none of them lands on a chip
    // or in the dropped count.
    unsigned char stream[16 * MD_SIZE];
    size_t n = 0;

    make_md(stream + (n++) * MD_SIZE, 0x7, 0);
    for (unsigned chip = 0; chip <= 3; ++chip) {
        make_md(stream + (n++) * MD_SIZE, 0x4, 0xABCDEF); // trigger, low half
        make_md(stream + (n++) * MD_SIZE, chip, 0x123456 + chip);
        make_md(stream + (n++) * MD_SIZE, 0x6, 0x123456); // trigger, high half
    }
    make_md(stream + (n++) * MD_SIZE, 0xC, 4);

    struct stats s;
    (void) run_stream(2, stream, n, &s);

    // The four pixels, one per chip, and nothing the triggers added, took or
    // left behind as dropped.
    KT_CHECK_EQ(s.pixels, 4u);
    KT_CHECK_EQ(s.chips_seen, 0xFu);
    KT_CHECK_EQ(s.frames_started, 1);
    KT_CHECK_EQ(s.frames_ended, 1);
}

static void
test_frame_lifecycle_is_generation_independent(void)
{
    unsigned char stream[4 * MD_SIZE];
    size_t n = 0;
    make_md(stream + (n++) * MD_SIZE, 0x7, 0);
    make_md(stream + (n++) * MD_SIZE, 0xC, 0);

    for (uint8_t gen = 1; gen <= 2; ++gen) {
        struct stats s;
        (void) run_stream(gen, stream, n, &s);
        KT_CHECK_EQ(s.frames_started, 1);
        KT_CHECK_EQ(s.frames_ended, 1);
    }
}

int
main(void)
{
    KT_RUN(test_gen2_decodes_all_four_chip_headers);
    KT_RUN(test_gen1_loses_every_gen2_pixel);
    KT_RUN(test_gen2_does_not_mistake_a_trigger_for_a_pixel);
    KT_RUN(test_gen2_triggers_between_pixels_disturb_nothing);
    KT_RUN(test_frame_lifecycle_is_generation_independent);
    return kt_summary();
}
