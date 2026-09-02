// Regression test for issue #16: buffered pixels are discarded when an
// acquisition ends without a frame-finished datum (e.g. times out).
//
// Drives the real read loop over a localhost UDP socket with a crafted
// measurement-data stream, queued in with a second katherine_udp_t sender.
// No hardware required.

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <katherine/acquisition.h>
#include <katherine/toa.h>
#include <katherine/device.h>
#include <katherine/error.h>
#include <katherine/udp.h>

#include "ktest.h"

#define MD_SIZE     6

// High, uncommon ports of their own, distinct from every other fixed pair
// registered in this tree (test_cmd_encoders.c's 42600/42601,
// test_udp_pinning.c's 42555-42557, bench_udp.c's
// 47301/47302/47311/47312, the ksim daemon's 1555/1556): this fixture
// claims no global resource, so the test needs no exclusive slot among the
// others.
#define PORT_DATA   42610
#define PORT_SENDER 42611

static void
make_md(unsigned char *dst, unsigned header, uint64_t payload)
{
    uint64_t w = ((uint64_t) (header & 0xF) << 44) | (payload & ((1ULL << 44) - 1));
    for (int i = 0; i < MD_SIZE; ++i) {
        dst[i] = (unsigned char) (w >> (8 * i));
    }
}

struct stats {
    size_t pixels_received_total;
    int pixels_received_calls;
    int frames_started;
    int frames_ended;
    bool last_completed_arg;
    bool last_completed_info;
    uint64_t last_received_pixels;
};

static void
pixels_received(void *ctx, const void *px, size_t count)
{
    struct stats *s = ctx;
    (void) px;
    s->pixels_received_total += count;
    ++s->pixels_received_calls;
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
    ++s->frames_ended;
    s->last_completed_arg   = completed;
    s->last_completed_info  = info->completed;
    s->last_received_pixels = info->received_pixels;
}

static int
run_case(bool send_frame_finished, struct stats *s)
{
    katherine_device_t dev;
    // Zeroed whole: this device is built by hand rather than by
    // katherine_device_init(), and katherine_device_t carries fields beyond
    // the two sessions -- the borrowed acquisition among them, which
    // katherine_device_fini() acts on.
    memset(&dev, 0, sizeof(dev));
    memset(&dev, 0, sizeof(dev));

    // Only the data socket is used by the read loop. 100 ms recv timeout.
    // Bound to a fixed port on loopback, so the sender below can be pointed
    // at it directly instead of discovering an OS-picked one.
    int res = katherine_udp_init_bound(&dev.data_socket, "127.0.0.1", PORT_DATA, "127.0.0.1", 1, 100);
    KT_CHECK(res == 0);
    if (res != 0) {
        return res;
    }

    katherine_acquisition_t acq;
    memset(&acq, 0, sizeof(acq));
    res = katherine_acquisition_init(&acq, &dev, s,
        1024 * MD_SIZE /* md buffer */,
        65536 /* pixel buffer, never fills */,
        0 /* report_timeout disabled */,
        1 /* fail_timeout ms */);
    KT_CHECK(res == 0);
    if (res != 0) {
        katherine_acquisition_fini(&acq);
        katherine_udp_fini(&dev.data_socket);
        return -1;
    }

    acq.handlers.pixels_received = pixels_received;
    acq.handlers.frame_started   = frame_started;
    acq.handlers.frame_ended     = frame_ended;

    // Stand in for katherine_acquisition_begin (which needs hardware).
    acq.state = ACQUISITION_RUNNING;
    // Resolved by katherine_acquisition_begin() in a real run; the decoder is
    // instantiated per pixel-clock divider and refuses to guess one.
    acq.toa_coarse_tick_to_fine_shift = katherine_tpx3_toa_coarse_tick_to_fine_shift(FREQ_40);
    acq.last_toa_offset               = katherine_tpx3_toa_coarse_tick_to_fine_ticks(FREQ_40);
    acq.acq_mode                      = ACQUISITION_MODE_TOA_TOT;
    acq.fast_vco_enabled              = false;
    acq.decode_data                   = true;
    acq.requested_frames              = 1;
    acq.requested_frame_duration      = 0.0;
    acq.acq_start_time                = time(NULL);

    // Craft the stream: frame start, 5 pixels, optionally frame finished.
    unsigned char packet[7 * MD_SIZE];
    size_t n = 0;
    make_md(packet + (n++) * MD_SIZE, 0x7, 0);
    for (int i = 0; i < 5; ++i) {
        make_md(packet + (n++) * MD_SIZE, 0x4, 0x123456 + i);
    }
    if (send_frame_finished) {
        make_md(packet + (n++) * MD_SIZE, 0xC, 5 /* n_sent */);
    }

    katherine_udp_t sender;
    KT_CHECK(katherine_udp_init_bound(&sender, "127.0.0.1", PORT_SENDER, "127.0.0.1", PORT_DATA, 0) == 0);
    KT_CHECK(katherine_udp_send_exact(&sender, packet, n * MD_SIZE) == 0);
    katherine_udp_fini(&sender);

    res = katherine_acquisition_read(&acq);

    katherine_acquisition_fini(&acq);
    katherine_udp_fini(&dev.data_socket);
    return res;
}

// Case 1: normal completion -- behavior must be unchanged.
static void
test_case_completed(void)
{
    struct stats s;
    memset(&s, 0, sizeof(s));
    int res = run_case(true, &s);
    printf("case 1 (completed):   res=%d pixels=%zu started=%d ended=%d "
           "completed_arg=%d info.completed=%d info.received=%llu\n",
        res, s.pixels_received_total, s.frames_started, s.frames_ended,
        s.last_completed_arg, s.last_completed_info,
        (unsigned long long) s.last_received_pixels);

    KT_CHECK(res == 0);
    KT_CHECK(s.pixels_received_total == 5);
    KT_CHECK(s.frames_started == 1);
    KT_CHECK(s.frames_ended == 1);
    KT_CHECK(s.last_completed_arg == true);
    KT_CHECK(s.last_completed_info == true);
    KT_CHECK(s.last_received_pixels == 5);
}

// Case 2: acquisition times out mid-frame -- issue #16.
static void
test_case_interrupted(void)
{
    struct stats s;
    memset(&s, 0, sizeof(s));
    int res = run_case(false, &s);
    printf("case 2 (interrupted): res=%d pixels=%zu started=%d ended=%d "
           "completed_arg=%d info.completed=%d info.received=%llu\n",
        res, s.pixels_received_total, s.frames_started, s.frames_ended,
        s.last_completed_arg, s.last_completed_info,
        (unsigned long long) s.last_received_pixels);

    KT_CHECK(res == KATHERINE_E_TIMEOUT);
    KT_CHECK(s.pixels_received_total == 5); /* previously 0: pixels were dropped */
    KT_CHECK(s.frames_started == 1);
    KT_CHECK(s.frames_ended == 1); /* previously 0: unbalanced handlers */
    KT_CHECK(s.last_completed_arg == false);
    KT_CHECK(s.last_completed_info == false);
    KT_CHECK(s.last_received_pixels == 5);
}

int
main(void)
{
    KT_RUN(test_case_completed);
    KT_RUN(test_case_interrupted);
    return kt_summary();
}
