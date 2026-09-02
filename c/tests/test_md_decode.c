/**
 * \file
 * \brief Decoding tests over synthetic measurement data.
 *
 * These tests drive the real read loop of c/src/acquisition.c over a crafted
 * measurement data stream, with no readout and no emulator involved: the
 * datagrams are queued in with a second katherine_udp_t sender before the
 * loop starts, exactly as test_issue16.c does it, and the loop reads them
 * in order.
 *
 * Every datum is built with the field declarations of c/src/md.h, the ones
 * the decoder reads them back with, so that the two can never disagree.
 *
 * \author Petr Mánek
 * \date 21.8.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <katherine/acquisition.h>
#include <katherine/device.h>
#include <katherine/toa.h>
#include <katherine/udp.h>

// The pixel mapping functions of md.h are written against the acquisition,
// so its declaration has to precede them.
#include "protocol/md.h"

#include "ktest.h"

// Measurement data headers, as dispatched by the read loop.
#define MD_HDR_PIXEL          0x4
#define MD_HDR_TIME_OFFSET    0x5
#define MD_HDR_NEW_FRAME      0x7
#define MD_HDR_FRAME_FINISHED 0xC

// High, uncommon ports of their own, distinct from every other fixed pair
// registered in this tree (test_cmd_encoders.c's 42600/42601,
// test_issue16.c's 42610/42611, test_udp_pinning.c's 42555-42557,
// bench_udp.c's 47301/47302/47311/47312, the ksim daemon's 1555/1556): this
// fixture claims no global resource, so the test needs no exclusive slot
// among the others.
#define PORT_DATA             42620
#define PORT_SENDER           42621

// The timestamp offset counts whole coarse time-of-arrival windows, i.e.
// multiples of the 1 << 14 ticks the coarse field holds.
#define TOA_WINDOW            (1ull << 14)

// Timestamps are decoded into fine-oscillator ticks, so every coarse quantity
// below is compared after scaling by the ratio for the configured frequency.
// Named once here and asserted against the library in test_fine_ticks_pin.
#define TOA_FREQ              FREQ_40
#define TOA_FINE              16ull
#define TOA_FINE_SHIFT        4u

// Recv timeout of the acquisition's data socket. Reached once per run, after
// the last datagram, so it also bounds how long a run lingers.
#define RECV_TIMEOUT_MS       100

// Only reached if the stream fails to end the acquisition, which every case
// below asserts against; it keeps a broken read loop from hanging.
#define FAIL_TIMEOUT_MS       2000

// Buffers of the acquisition under test: the measurement data buffer is far
// larger than any datagram sent here, and the pixel buffer holds every hit
// of a run, so hits reach the handler only when a frame ends.
#define MD_BUFFER_MDS         256
#define PIXEL_BUFFER_HITS     64

// The acquisition mode under test, and the pixel layout it delivers.
typedef katherine_px_toa_tot_t px_t;

// ------------------------------------------------------------------
// Stream construction.

static uint64_t
make_new_frame(void)
{
    return INSERT((uint64_t) 0, md, header, (uint64_t) MD_HDR_NEW_FRAME);
}

static uint64_t
make_time_offset(uint32_t offset)
{
    uint64_t md = INSERT((uint64_t) 0, md, header, (uint64_t) MD_HDR_TIME_OFFSET);
    return INSERT(md, md_time_offset, offset, (uint64_t) offset);
}

static uint64_t
make_frame_finished(uint64_t n_sent)
{
    uint64_t md = INSERT((uint64_t) 0, md, header, (uint64_t) MD_HDR_FRAME_FINISHED);
    return INSERT(md, md_frame_finished, n_sent, n_sent);
}

static uint64_t
make_pixel(uint8_t x, uint8_t y, uint16_t toa)
{
    uint64_t md = INSERT((uint64_t) 0, md, header, (uint64_t) MD_HDR_PIXEL);
    md          = INSERT(md, pmd_toa_tot, coord_x, (uint64_t) x);
    md          = INSERT(md, pmd_toa_tot, coord_y, (uint64_t) y);
    md          = INSERT(md, pmd_toa_tot, toa, (uint64_t) toa);
    md          = INSERT(md, pmd_toa_tot, hit_count, (uint64_t) 1);
    return INSERT(md, pmd_toa_tot, tot, (uint64_t) 100);
}

// Appends one datum in wire order (little endian) at the given index.
static void
store_md(unsigned char *stream, size_t index, uint64_t md)
{
    unsigned char *dst = stream + index * KATHERINE_MD_SIZE;
    for (size_t i = 0; i < KATHERINE_MD_SIZE; ++i) {
        dst[i] = (unsigned char) (md >> (8 * i));
    }
}

// ------------------------------------------------------------------
// Fixture.

typedef struct decode_probe {
    uint32_t frames_started;
    uint32_t frames_ended;

    size_t hits; /* summed over every pixels_received call */
    uint64_t toa[PIXEL_BUFFER_HITS];

    // Copied out of the acquisition once the read loop has returned.
    char state;
    int completed_frames;
    size_t dropped;
} decode_probe_t;

static void
on_frame_started(void *ctx, int frame_idx)
{
    decode_probe_t *probe = (decode_probe_t *) ctx;

    KT_CHECK_EQ(frame_idx, probe->frames_started);
    ++probe->frames_started;
}

static void
on_frame_ended(void *ctx, int frame_idx, bool completed, const katherine_frame_info_t *info)
{
    decode_probe_t *probe = (decode_probe_t *) ctx;

    (void) completed;
    (void) info;

    KT_CHECK_EQ(frame_idx, probe->frames_ended);
    ++probe->frames_ended;
}

static void
on_pixels_received(void *ctx, const void *px, size_t count)
{
    decode_probe_t *probe = (decode_probe_t *) ctx;
    const px_t *hits      = (const px_t *) px;

    for (size_t i = 0; i < count; ++i) {
        if (probe->hits + i < PIXEL_BUFFER_HITS) probe->toa[probe->hits + i] = hits[i].timestamp;
    }

    probe->hits += count;
}

// Runs one acquisition over the given stream, which is cut into `datagrams`
// consecutive datagrams of the given lengths, and returns what
// katherine_acquisition_read() returned. The datagrams are all sent before
// the loop starts, so the loop reads them back to back and the run ends on
// the frame-finished datum of the last frame the stream carries.
static int
run_stream(const unsigned char *stream, const size_t *datagram_len, size_t datagrams, int frames,
    decode_probe_t *probe)
{
    katherine_device_t dev;
    // Zeroed whole: this device is built by hand rather than by
    // katherine_device_init(), and katherine_device_t carries fields beyond
    // the two sessions -- the borrowed acquisition among them, which
    // katherine_device_fini() acts on.
    memset(&dev, 0, sizeof(dev));
    memset(&dev, 0, sizeof(dev));
    memset(probe, 0, sizeof(*probe));

    // Only the data socket is used by the read loop. Bound to a fixed port
    // on loopback, so the sender below can be pointed at it directly
    // instead of discovering an OS-picked one.
    int res = katherine_udp_init_bound(&dev.data_socket, "127.0.0.1", PORT_DATA, "127.0.0.1", 1, RECV_TIMEOUT_MS);
    KT_CHECK(res == 0);
    if (res != 0) {
        return res;
    }

    katherine_acquisition_t acq;
    memset(&acq, 0, sizeof(acq));
    res = katherine_acquisition_init(&acq, &dev, probe, MD_BUFFER_MDS * KATHERINE_MD_SIZE,
        PIXEL_BUFFER_HITS * sizeof(px_t), 0 /* report_timeout disabled */, FAIL_TIMEOUT_MS);
    KT_CHECK(res == 0);
    if (res != 0) {
        katherine_udp_fini(&dev.data_socket);
        return -1;
    }

    acq.handlers.frame_started   = on_frame_started;
    acq.handlers.frame_ended     = on_frame_ended;
    acq.handlers.pixels_received = on_pixels_received;

    // Stand in for katherine_acquisition_begin (which needs hardware). The
    // memset above leaves most of the decoding state it initializes -- the
    // pixel buffer counters and the timestamp offset -- zeroed, as it does.
    // The divider shift and the biased offset are the exceptions and must be
    // set: begin() resolves them from the configured frequency, and the read
    // loop refuses a divider it was not built for rather than guessing one.
    acq.toa_coarse_tick_to_fine_shift = katherine_tpx3_toa_coarse_tick_to_fine_shift(TOA_FREQ);
    acq.last_toa_offset               = TOA_FINE;
    acq.state                         = ACQUISITION_RUNNING;
    acq.acq_mode                      = ACQUISITION_MODE_TOA_TOT;
    acq.fast_vco_enabled              = false;
    acq.decode_data                   = true;
    acq.requested_frames              = frames;
    acq.requested_frame_duration      = 0.0;
    acq.acq_start_time                = time(NULL);

    katherine_udp_t sender;
    KT_CHECK(katherine_udp_init_bound(&sender, "127.0.0.1", PORT_SENDER, "127.0.0.1", PORT_DATA, 0) == 0);
    size_t offset = 0;
    for (size_t i = 0; i < datagrams; ++i) {
        KT_CHECK(katherine_udp_send_exact(&sender, stream + offset, datagram_len[i]) == 0);
        offset += datagram_len[i];
    }
    katherine_udp_fini(&sender);

    res = katherine_acquisition_read(&acq);

    probe->state            = acq.state;
    probe->completed_frames = acq.completed_frames;
    probe->dropped          = acq.dropped_measurement_data;

    katherine_acquisition_fini(&acq);
    katherine_udp_fini(&dev.data_socket);
    return res;
}

// TOA_FINE is written out above so the expectations stay readable. If the
// library's ratio for TOA_FREQ ever differs, every scaled expectation here is
// wrong in the same direction and would still agree with a decoder that had
// drifted the same way, so pin the constant to its source.
static void
test_fine_ticks_pin(void)
{
    KT_CHECK_EQ(katherine_tpx3_toa_coarse_tick_to_fine_ticks(TOA_FREQ), TOA_FINE);
    KT_CHECK_EQ(katherine_tpx3_toa_coarse_tick_to_fine_shift(TOA_FREQ), TOA_FINE_SHIFT);

    // The two accessors describe the same ratio, so the shift must reproduce
    // it exactly -- at every frequency, not only the one used here.
    const katherine_freq_t every[] = {FREQ_20, FREQ_40, FREQ_80, FREQ_160};
    for (size_t i = 0; i < sizeof(every) / sizeof(every[0]); ++i) {
        KT_CHECK_EQ(1u << katherine_tpx3_toa_coarse_tick_to_fine_shift(every[i]),
            katherine_tpx3_toa_coarse_tick_to_fine_ticks(every[i]));
    }
}

// ------------------------------------------------------------------
// Combining itself: coarse and fine counters into one timestamp.

// The decoders are static inline in md.h, so the arithmetic can be exercised
// directly on a hand-built word. That is worth doing separately from the
// stream tests, which run one format at one divider and would not notice the
// fine term at all.
//
// Each divider has its own instantiation, so the switch here is not incidental
// -- it is how the test reaches all four, and a mis-wired table would show up
// as one arm disagreeing with the rest.
static uint64_t
combine_f_toa_tot(uint16_t coarse, uint8_t ftoa, uint64_t offset, uint8_t fine_shift)
{
    uint64_t md = 0;
    md          = INSERT(md, pmd_f_toa_tot, coord_x, (uint64_t) 1);
    md          = INSERT(md, pmd_f_toa_tot, coord_y, (uint64_t) 2);
    md          = INSERT(md, pmd_f_toa_tot, toa, (uint64_t) coarse);
    md          = INSERT(md, pmd_f_toa_tot, ftoa, (uint64_t) ftoa);
    md          = INSERT(md, pmd_f_toa_tot, tot, (uint64_t) 100);

    // Mirroring katherine_acquisition_begin(): the epoch carries one whole
    // coarse tick of bias, on top of whatever offset the stream delivered.
    const uint64_t fine_ticks = (uint64_t) 1u << fine_shift;
    const uint64_t bias       = fine_ticks > 16 ? fine_ticks : 16;

    katherine_acquisition_t acq;
    memset(&acq, 0, sizeof(acq));
    acq.toa_coarse_tick_to_fine_shift = fine_shift;
    acq.last_toa_offset               = bias + offset;

    katherine_px_f_toa_tot_t dst;
    memset(&dst, 0, sizeof(dst));
    switch (fine_shift) {
    case 2:  pmd_f_toa_tot_s2_map(&dst, &md, &acq); break;
    case 3:  pmd_f_toa_tot_s3_map(&dst, &md, &acq); break;
    case 4:  pmd_f_toa_tot_s4_map(&dst, &md, &acq); break;
    case 5:  pmd_f_toa_tot_s5_map(&dst, &md, &acq); break;
    default: KT_CHECK(false); return 0;
    }
    return dst.timestamp;
}

// What the combined value is: the coarse count scaled into fine ticks, plus
// the epoch bias, less the fine term.
static void
test_combine_fine_term(void)
{
    KT_CHECK_EQ(combine_f_toa_tot(100, 0, 0, TOA_FINE_SHIFT), 100ull * TOA_FINE + 16);
    KT_CHECK_EQ(combine_f_toa_tot(100, 5, 0, TOA_FINE_SHIFT), 100ull * TOA_FINE + 16 - 5);
    KT_CHECK_EQ(combine_f_toa_tot(100, 15, 0, TOA_FINE_SHIFT), 100ull * TOA_FINE + 16 - 15);

    // The stream's offset is already in fine ticks, so it adds without scaling.
    KT_CHECK_EQ(combine_f_toa_tot(100, 5, 640, TOA_FINE_SHIFT), 100ull * TOA_FINE + 16 - 5 + 640);

    // Every divider has its own instantiation; a wrong one would scale here.
    KT_CHECK_EQ(combine_f_toa_tot(7, 3, 0, 5), 7ull * 32 + 32 - 3);
    KT_CHECK_EQ(combine_f_toa_tot(7, 3, 0, 4), 7ull * 16 + 16 - 3);
    KT_CHECK_EQ(combine_f_toa_tot(7, 3, 0, 3), 7ull * 8 + 16 - 3);
    KT_CHECK_EQ(combine_f_toa_tot(7, 3, 0, 2), 7ull * 4 + 16 - 3);
}

// The property the epoch bias exists for. The worst case is the first coarse
// tick of a window carrying the largest fine term: without the bias that
// subtraction goes below zero and wraps to about 1.8e19, a hit apparently 914
// years in the future. With it, the value stays positive and no per-hit test
// is needed.
static void
test_bias_makes_underflow_unrepresentable(void)
{
    for (uint8_t shift = 2; shift <= 5; ++shift) {
        const uint64_t fine_ticks = (uint64_t) 1u << shift;
        const uint64_t bias       = fine_ticks > 16 ? fine_ticks : 16;

        // The worst case: the largest fine term the field can hold, in the
        // earliest coarse tick of a window. One coarse tick of bias would not
        // cover this at the shorter dividers, where a coarse tick is only 4 or
        // 8 fine ticks against a fine field spanning 16.
        const uint64_t worst = combine_f_toa_tot(0, 15, 0, shift);

        KT_CHECK_EQ(worst, bias - 15);
        KT_CHECK(worst > 0);
        KT_CHECK(worst < (uint64_t) 1 << 32); /* nowhere near a wrap */

        // And the bias stays a whole multiple of the coarse tick, or it would
        // corrupt the residue the fine term is recovered from.
        KT_CHECK_EQ(bias % fine_ticks, 0u);
    }
}

static void
test_combine_is_injective(void)
{
    // The property the single field exists for: distinct (coarse, fine) pairs
    // must give distinct timestamps, and ordering must follow arrival time.
    // Coarse k spans [16k-15, 16k] and k+1 spans [16k+1, 16k+16] -- adjacent
    // and disjoint, so a sweep over both counters is strictly decreasing in
    // the fine term and strictly increasing across coarse ticks.
    uint64_t prev = 0;
    bool first    = true;
    for (uint16_t coarse = 1; coarse < 64; ++coarse) {
        for (int f = 15; f >= 0; --f) {
            const uint64_t t = combine_f_toa_tot(coarse, (uint8_t) f, 0, TOA_FINE_SHIFT);
            if (!first) KT_CHECK(t > prev);
            prev  = t;
            first = false;
        }
    }
}

// ------------------------------------------------------------------
// a) The timestamp offset belongs to the frame that delivered it.

// Offset of the first frame, and the coarse arrival times of the two hits.
// Both fit the 14-bit coarse field, so neither carries an offset of its
// own.
#define FRAME1_OFFSET 3u
#define FRAME1_TOA    0x1234
#define FRAME2_TOA    0x0567

static void
test_toa_offset_reset(void)
{
    // Two frames. The first delivers a nonzero timestamp offset ahead of
    // its hit; the second delivers none, so its hit arrives while the only
    // offset ever received belongs to the frame before it. The coarse time
    // of arrival restarts with the frame, hence so must the offset.
    unsigned char stream[7 * KATHERINE_MD_SIZE];
    size_t n = 0;
    store_md(stream, n++, make_new_frame());
    store_md(stream, n++, make_time_offset(FRAME1_OFFSET));
    store_md(stream, n++, make_pixel(1, 2, FRAME1_TOA));
    store_md(stream, n++, make_frame_finished(1));
    store_md(stream, n++, make_new_frame());
    store_md(stream, n++, make_pixel(3, 4, FRAME2_TOA));
    store_md(stream, n++, make_frame_finished(1));

    size_t datagram_len = n * KATHERINE_MD_SIZE;
    decode_probe_t probe;
    KT_CHECK_EQ(run_stream(stream, &datagram_len, 1, 2, &probe), 0);

    KT_CHECK_EQ(probe.state, ACQUISITION_SUCCEEDED);
    KT_CHECK_EQ(probe.completed_frames, 2);
    KT_CHECK_EQ(probe.dropped, 0);
    KT_CHECK_EQ(probe.frames_started, 2);
    KT_CHECK_EQ(probe.frames_ended, 2);

    KT_REQUIRE(probe.hits == 2);
    KT_CHECK_EQ(probe.toa[0], (FRAME1_TOA + FRAME1_OFFSET * TOA_WINDOW) * TOA_FINE + TOA_FINE);
    KT_CHECK_EQ(probe.toa[1], FRAME2_TOA * TOA_FINE + TOA_FINE);
}

// ------------------------------------------------------------------
// b) A datagram whose length is not a whole number of data.

// Length of the fragment the second datagram below ends with, and the
// arrival times of the four hits it and its predecessor carry.
#define TAIL_BYTES 3
#define HIT_TOA(i) (0x0101 * ((i) + 1))

static void
test_partial_datum_ignored(void)
{
    // Two datagrams, the second of which ends three bytes into a fourth
    // datum. The readout never cuts a datagram there, but a truncated one
    // arrives exactly like this, and the loop that walks the buffer in
    // six-byte steps must not step onto the fragment: the word it would
    // read there is the fragment followed by whatever the longer datagram
    // before it left further along the buffer.
    //
    // Those leftovers are arranged here to make the fragment look like a
    // hit, so that the over-read is observed rather than guessed at. The
    // fragment begins at offset 18, so the header nibble of the word read
    // there -- bits 44 to 47 -- comes from byte 23, which is the last byte
    // of the fourth datum of the first datagram, hence that datum's own
    // header: a pixel.
    unsigned char stream[8 * KATHERINE_MD_SIZE + TAIL_BYTES];
    memset(stream, 0, sizeof(stream));

    // First datagram: a whole frame of three hits.
    store_md(stream, 0, make_new_frame());
    store_md(stream, 1, make_pixel(1, 1, HIT_TOA(0)));
    store_md(stream, 2, make_pixel(2, 2, HIT_TOA(1)));
    store_md(stream, 3, make_pixel(3, 3, HIT_TOA(2)));
    store_md(stream, 4, make_frame_finished(3));

    // Second datagram: a whole frame of one hit, then the fragment left by
    // the memset above.
    store_md(stream, 5, make_new_frame());
    store_md(stream, 6, make_pixel(4, 4, HIT_TOA(3)));
    store_md(stream, 7, make_frame_finished(1));

    size_t datagram_len[2] = {5 * KATHERINE_MD_SIZE, 3 * KATHERINE_MD_SIZE + TAIL_BYTES};
    decode_probe_t probe;
    KT_CHECK_EQ(run_stream(stream, datagram_len, 2, 2, &probe), 0);

    KT_CHECK_EQ(probe.state, ACQUISITION_SUCCEEDED);
    KT_CHECK_EQ(probe.completed_frames, 2);
    KT_CHECK_EQ(probe.dropped, 0);
    KT_CHECK_EQ(probe.frames_started, 2);
    KT_CHECK_EQ(probe.frames_ended, 2);

    // Four data carried four hits: the fragment is not a fifth.
    KT_CHECK_EQ(probe.hits, 4);
    for (size_t i = 0; i < 4 && i < probe.hits; ++i) {
        KT_CHECK_EQ(probe.toa[i], HIT_TOA(i) * TOA_FINE + TOA_FINE);
    }
}

// ------------------------------------------------------------------

int
main(void)
{
    KT_RUN(test_fine_ticks_pin);
    KT_RUN(test_combine_fine_term);
    KT_RUN(test_bias_makes_underflow_unrepresentable);
    KT_RUN(test_combine_is_injective);
    KT_RUN(test_toa_offset_reset);
    KT_RUN(test_partial_datum_ignored);
    return kt_summary();
}
