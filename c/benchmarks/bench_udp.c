/**
 * @file
 * @brief UDP loopback throughput benchmark (B2): measurement bursts and a
 * command round trip, generalizing misc/bench-seeds/pin_bench.c.
 *
 * Two independent measurements, each over its own katherine_udp_init_bound()
 * pair on 127.0.0.1 and its own pair of high ports so the two cannot collide:
 *
 *   - udp_md_dgrams / udp_md_throughput: bursts of 1356-byte,
 *     measurement-sized datagrams sent and drained one way, receiver pinned
 *     (katherine_udp_pin_remote()) exactly as c/src/device.c pins the real
 *     data socket -- the shipped configuration, not an unpinned alternative.
 *
 *   - udp_cmd_roundtrips: 8-byte, command-sized datagrams sent one way and
 *     echoed back. There is no second thread: loopback delivery is complete
 *     by the time katherine_udp_send_exact() returns, so the "helper" that
 *     stands in for a device replying to a command is just the next few
 *     lines of this same call, run inline. This whole measurement is
 *     same-process loopback, so its op/s is an upper bound -- a real
 *     round trip additionally crosses into and back out of the readout's own
 *     firmware, which this cannot model.
 *
 * @author Petr Mánek
 * @date 24.8.26
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <katherine/udp.h>

/* High, uncommon ports of their own -- distinct from c/tests/test_udp_pinning
   (42555-42557) and the ksim daemon (1555/1556) -- so this benchmark claims
   no port anything else registered in this tree might be using. */
#define PORT_MD_TX        47301
#define PORT_MD_RX        47302
#define PORT_CMD_A        47311
#define PORT_CMD_B        47312

#define LOOPBACK_ADDR     "127.0.0.1"
#define UDP_TIMEOUT_MS    1000

#define MD_DGRAM_BYTES    1356
#define MD_BURST          32
#define CMD_DGRAM_BYTES   8

/* Every measurement keeps re-running its loop until it has measured at
   least this many seconds, so that a fast host still gets a stable rate.
   The KATHERINE_BENCH_MIN_SECONDS environment variable overrides the
   default, for the same calibration purpose as in bench_decode. */
#define BENCH_MIN_SECONDS 2.0

static double g_min_seconds = BENCH_MIN_SECONDS;

static double
now_s(void)
{
    struct timespec ts;
    (void) clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double) ts.tv_sec + 1e-9 * (double) ts.tv_nsec;
}

/* Bursts of MD_BURST measurement-sized datagrams sent then drained, over a
   pinned receiver -- the shipped configuration of katherine_device_t's data
   socket (see katherine_udp_pin_remote() in c/src/device.c). Reports
   udp_md_dgrams and udp_md_throughput, both from the one measured run: the
   two are the same count read out in different units, so nothing is gained
   by timing them separately. */
static int
run_md_throughput(void)
{
    katherine_udp_t tx, rx;
    static char buf[MD_DGRAM_BYTES];
    uint64_t dgrams = 0;
    double t0, dt;

    if (katherine_udp_init_bound(&tx, LOOPBACK_ADDR, PORT_MD_TX, LOOPBACK_ADDR, PORT_MD_RX, UDP_TIMEOUT_MS)) {
        return 1;
    }
    if (katherine_udp_init_bound(&rx, LOOPBACK_ADDR, PORT_MD_RX, LOOPBACK_ADDR, PORT_MD_TX, UDP_TIMEOUT_MS)) {
        katherine_udp_fini(&tx);
        return 1;
    }
    katherine_udp_pin_remote(&rx);

    memset(buf, 0x5A, sizeof(buf));

    t0 = now_s();
    do {
        for (int b = 0; b < MD_BURST; ++b) {
            if (katherine_udp_send_exact(&tx, buf, MD_DGRAM_BYTES)) {
                katherine_udp_fini(&tx);
                katherine_udp_fini(&rx);
                return 1;
            }
        }
        for (int b = 0; b < MD_BURST; ++b) {
            size_t n = sizeof(buf);
            if (katherine_udp_recv(&rx, buf, &n) || n != MD_DGRAM_BYTES) {
                katherine_udp_fini(&tx);
                katherine_udp_fini(&rx);
                return 1;
            }
        }
        dgrams += MD_BURST;
        dt = now_s() - t0;
    } while (dt < g_min_seconds);

    double dgram_s = (double) dgrams / dt;
    double mb_s    = dgram_s * MD_DGRAM_BYTES / 1e6;

    printf("{\"bench\":\"udp_md_dgrams\",\"value\":%.2f,\"unit\":\"dgram/s\"}\n", dgram_s);
    printf("{\"bench\":\"udp_md_throughput\",\"value\":%.2f,\"unit\":\"MB/s\"}\n", mb_s);
    fflush(stdout);

    katherine_udp_fini(&tx);
    katherine_udp_fini(&rx);
    return 0;
}

/* Command-sized (8-byte) request/reply round trips between two sessions in
   this same process: A sends, B (the "helper loop", run inline right here)
   receives and echoes back, A receives the echo. See the file comment for
   why this models an upper bound rather than a real slow-control
   transaction. Reports udp_cmd_roundtrips. */
static int
run_cmd_roundtrips(void)
{
    katherine_udp_t a, b;
    static char cmd[CMD_DGRAM_BYTES];
    static char reply[CMD_DGRAM_BYTES];
    uint64_t roundtrips = 0;
    double t0, dt;
    int failed = 0;

    if (katherine_udp_init_bound(&a, LOOPBACK_ADDR, PORT_CMD_A, LOOPBACK_ADDR, PORT_CMD_B, UDP_TIMEOUT_MS)) {
        return 1;
    }
    if (katherine_udp_init_bound(&b, LOOPBACK_ADDR, PORT_CMD_B, LOOPBACK_ADDR, PORT_CMD_A, UDP_TIMEOUT_MS)) {
        katherine_udp_fini(&a);
        return 1;
    }

    memset(cmd, 0xC5, sizeof(cmd));

    t0 = now_s();
    do {
        size_t n = sizeof(reply);

        if (katherine_udp_send_exact(&a, cmd, CMD_DGRAM_BYTES)) {
            failed = 1;
            break;
        }

        /* The helper loop: the responder side, right here in the same
           thread -- loopback delivery is already complete by the time
           send_exact() above returned, so there is nothing to wait for. */
        if (katherine_udp_recv(&b, reply, &n) || n != CMD_DGRAM_BYTES) {
            failed = 1;
            break;
        }
        if (katherine_udp_send_exact(&b, reply, CMD_DGRAM_BYTES)) {
            failed = 1;
            break;
        }

        n = sizeof(reply);
        if (katherine_udp_recv(&a, reply, &n) || n != CMD_DGRAM_BYTES) {
            failed = 1;
            break;
        }

        ++roundtrips;
        dt = now_s() - t0;
    } while (dt < g_min_seconds);

    if (!failed) {
        printf("{\"bench\":\"udp_cmd_roundtrips\",\"value\":%.2f,\"unit\":\"op/s\"}\n", (double) roundtrips / dt);
        fflush(stdout);
    }

    katherine_udp_fini(&a);
    katherine_udp_fini(&b);
    return failed;
}

int
main(void)
{
    const char *env = getenv("KATHERINE_BENCH_MIN_SECONDS");
    if (env != NULL) {
        double v = strtod(env, NULL);
        if (v > 0) g_min_seconds = v;
    }

    int res = 0;

    res |= run_md_throughput();
    res |= run_cmd_roundtrips();

    return res;
}
