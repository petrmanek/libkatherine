/* Regression test for issue #16: buffered pixels are discarded when an
 * acquisition ends without a frame-finished datum (e.g. times out).
 *
 * Drives the real read loop over a localhost UDP socket with a crafted
 * measurement-data stream. No hardware required.
 */

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <katherine/acquisition.h>
#include <katherine/device.h>
#include <katherine/udp.h>

#define MD_SIZE 6

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
    memset(&dev, 0, sizeof(dev));

    /* Only the data socket is used by the read loop. 100 ms recv timeout. */
    int res = katherine_udp_init(&dev.data_socket, 0, "127.0.0.1", 1, 100);
    assert(res == 0);

    struct sockaddr_in bound;
    socklen_t bound_len = sizeof(bound);
    assert(getsockname(dev.data_socket.sock, (struct sockaddr *) &bound, &bound_len) == 0);
    bound.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    katherine_acquisition_t acq;
    memset(&acq, 0, sizeof(acq));
    res = katherine_acquisition_init(&acq, &dev, s,
        1024 * MD_SIZE /* md buffer */,
        65536 /* pixel buffer, never fills */,
        0 /* report_timeout disabled */,
        1 /* fail_timeout ms */);
    assert(res == 0);

    acq.handlers.pixels_received = pixels_received;
    acq.handlers.frame_started   = frame_started;
    acq.handlers.frame_ended     = frame_ended;

    /* Stand in for katherine_acquisition_begin (which needs hardware). */
    acq.state                    = ACQUISITION_RUNNING;
    acq.acq_mode                 = ACQUISITION_MODE_TOA_TOT;
    acq.fast_vco_enabled         = false;
    acq.decode_data              = true;
    acq.requested_frames         = 1;
    acq.requested_frame_duration = 0.0;
    acq.acq_start_time           = time(NULL);

    /* Craft the stream: frame start, 5 pixels, optionally frame finished. */
    unsigned char packet[7 * MD_SIZE];
    size_t n = 0;
    make_md(packet + (n++) * MD_SIZE, 0x7, 0);
    for (int i = 0; i < 5; ++i) {
        make_md(packet + (n++) * MD_SIZE, 0x4, 0x123456 + i);
    }
    if (send_frame_finished) {
        make_md(packet + (n++) * MD_SIZE, 0xC, 5 /* n_sent */);
    }

    int sender = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    assert(sender >= 0);
    assert(sendto(sender, packet, n * MD_SIZE, 0,
               (struct sockaddr *) &bound, sizeof(bound))
        == (ssize_t) (n * MD_SIZE));
    close(sender);

    res = katherine_acquisition_read(&acq);

    katherine_acquisition_fini(&acq);
    katherine_udp_fini(&dev.data_socket);
    return res;
}

int
main(void)
{
    /* Case 1: normal completion — behavior must be unchanged. */
    struct stats s1;
    memset(&s1, 0, sizeof(s1));
    int res = run_case(true, &s1);
    printf("case 1 (completed):   res=%d pixels=%zu started=%d ended=%d "
           "completed_arg=%d info.completed=%d info.received=%llu\n",
        res, s1.pixels_received_total, s1.frames_started, s1.frames_ended,
        s1.last_completed_arg, s1.last_completed_info,
        (unsigned long long) s1.last_received_pixels);
    assert(res == 0);
    assert(s1.pixels_received_total == 5);
    assert(s1.frames_started == 1);
    assert(s1.frames_ended == 1);
    assert(s1.last_completed_arg == true);
    assert(s1.last_completed_info == true);
    assert(s1.last_received_pixels == 5);

    /* Case 2: acquisition times out mid-frame — issue #16. */
    struct stats s2;
    memset(&s2, 0, sizeof(s2));
    res = run_case(false, &s2);
    printf("case 2 (interrupted): res=%d pixels=%zu started=%d ended=%d "
           "completed_arg=%d info.completed=%d info.received=%llu\n",
        res, s2.pixels_received_total, s2.frames_started, s2.frames_ended,
        s2.last_completed_arg, s2.last_completed_info,
        (unsigned long long) s2.last_received_pixels);
    assert(res == ETIMEDOUT);
    assert(s2.pixels_received_total == 5); /* previously 0: pixels were dropped */
    assert(s2.frames_started == 1);
    assert(s2.frames_ended == 1); /* previously 0: unbalanced handlers */
    assert(s2.last_completed_arg == false);
    assert(s2.last_completed_info == false);
    assert(s2.last_received_pixels == 5);

    printf("all assertions passed\n");
    return 0;
}
