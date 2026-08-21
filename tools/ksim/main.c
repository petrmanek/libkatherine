/**
 * @file
 * @brief Standalone UDP daemon fronting the protocol emulator.
 * @author Petr Mánek
 * @date 21.8.26
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

// Must be the very first thing in the file, before any #include (even
// <errno.h> below): monoclock.h and stopsig.h each guard their own
// clock_gettime()/sigaction() declarations behind _POSIX_C_SOURCE, but that
// only has an effect the first time any libc header pulls in <features.h> --
// glibc resolves POSIX visibility once for the whole translation unit and
// does not revisit it -- so it has to be set before literally anything else
// touches libc, not just before those two headers' own includes. Harmless on
// Windows, whose headers do not gate on it.
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// katherine/udp.h must precede monoclock.h/stopsig.h below: on Windows, it
// transitively pulls in <winsock2.h> (via udp_win.h) ahead of <windows.h>,
// which those two headers include directly. windows.h internally drags in
// the legacy Winsock 1.1 header unless Winsock 2 was already established
// first, so the reverse order does not compile under the Windows SDK.
#include <katherine/emulator.h>
#include <katherine/udp.h>

#include "args.h"
#include "monoclock.h"
#include "stopsig.h"

/* struct in_addr comes in transitively via katherine/udp.h (through
   udp_nix.h's <arpa/inet.h> or udp_win.h's <winsock2.h>, depending on
   platform), which is also where katherine_udp_t's own addr_remote field
   gets its struct sockaddr_in / SOCKADDR_IN type from. Nothing else here
   needs a directly-included sockets header: the client's address is
   formatted by hand instead of via inet_ntop(), which Windows only
   declares in <ws2tcpip.h>. */

/* This daemon bridges the deterministic protocol emulator (katherine/emulator.h)
 * to real UDP sockets, so that an unmodified katherine_device_t client on the
 * network -- or on the same host, using non-default ports -- can talk to it
 * as if it were a readout. It owns the sockets (via libkatherine's own
 * katherine_udp_* facility) and the wall clock; the emulator itself never
 * learns either, which keeps recorded runs comparable across machines.
 *
 * Platform specifics (long-option parsing, the stop signal, the monotonic
 * clock) live in the small private headers alongside this file (args.h,
 * stopsig.h, monoclock.h); this file itself has no #ifdef of its own, and
 * builds the same way on POSIX and Windows.
 */

/* ---------------------------------------------------------------------- */
/* Defaults and constants.                                                 */

#define DEFAULT_LISTEN_ADDR      "0.0.0.0"
#define DEFAULT_CTL_PORT         1555
#define DEFAULT_DATA_PORT        1556
#define DEFAULT_CLIENT_DATA_PORT 1556
#define DEFAULT_PROFILE          "gen1-tpx3"

/* Receive timeout of the control socket. Blocking on katherine_udp_recv()
   for at most this long is what paces the main loop -- short enough that
   the virtual clock tracks real time closely without busy-looping, and
   without a separate poll(). */
#define CTL_RECV_TIMEOUT_MS      1

/* Large enough for any control datagram the protocol defines, including a
   whole pixel-configuration chunk. */
#define RECV_BUF_SIZE            2048

/* Size of one pixel-configuration chunk, as sent by katherine_set_all_pixel_config(). */
#define PX_CHUNK_SIZE            1024

/* A measurement data datagram carries whole 6-byte data at up to 1356 bytes
   (226 of them), matching the chunking of the real readout. */
#define MD_DATAGRAM_MAX_MDS      226
#define MD_DATAGRAM_MAX_BYTES    (MD_DATAGRAM_MAX_MDS * KATHERINE_EMU_MD_SIZE)

/* Upper bound on control datagrams drained per main loop tick, so that a
   burst (e.g. a pixel-configuration upload) cannot starve the CRD/MD pumps
   below indefinitely. */
#define MAX_DRAIN_PER_TICK       256

/* Batch size for draining the command log into --log. */
#define LOG_DRAIN_BATCH          64

/* ---------------------------------------------------------------------- */
/* Command line options.                                                   */

enum {
    OPT_LISTEN = 256,
    OPT_CTL_PORT,
    OPT_DATA_PORT,
    OPT_CLIENT_DATA_PORT,
    OPT_PROFILE,
    OPT_SEED,
    OPT_RATE,
    OPT_HITS_PER_FRAME,
    OPT_LOST_PER_FRAME,
    OPT_PATTERN,
    OPT_ACK_LATENCY_US,
    OPT_DROP_PX_CHUNK,
    OPT_STRAY_CRD,
    OPT_LOG,
};

static const ksim_opt_t OPTS[] = {
    {"listen", '\0', true, OPT_LISTEN},
    {"ctl-port", '\0', true, OPT_CTL_PORT},
    {"data-port", '\0', true, OPT_DATA_PORT},
    {"client-data-port", '\0', true, OPT_CLIENT_DATA_PORT},
    {"profile", '\0', true, OPT_PROFILE},
    {"seed", '\0', true, OPT_SEED},
    {"rate", '\0', true, OPT_RATE},
    {"hits-per-frame", '\0', true, OPT_HITS_PER_FRAME},
    {"lost-per-frame", '\0', true, OPT_LOST_PER_FRAME},
    {"pattern", '\0', true, OPT_PATTERN},
    {"ack-latency-us", '\0', true, OPT_ACK_LATENCY_US},
    {"drop-px-chunk", '\0', true, OPT_DROP_PX_CHUNK},
    {"stray-crd", '\0', false, OPT_STRAY_CRD},
    {"log", '\0', true, OPT_LOG},
    {"quiet", 'q', false, 'q'},
    {"help", 'h', false, 'h'},
    {NULL, '\0', false, 0},
};

typedef struct daemon_options {
    const char *listen_addr;
    uint16_t ctl_port;
    uint16_t data_port;
    uint16_t client_data_port;
    const char *profile_name;

    uint64_t seed;
    bool seed_set;

    uint64_t rate;
    bool rate_set;

    uint32_t hits_per_frame;
    bool hits_set;

    uint32_t lost_per_frame;
    bool lost_set;

    katherine_emu_pattern_t pattern;
    bool pattern_set;

    uint64_t ack_latency_us;
    bool ack_latency_set;

    uint32_t drop_px_chunk;
    bool stray_crd;

    const char *log_path;
    bool quiet;
} daemon_options_t;

static void
print_usage(FILE *out, const char *prog)
{
    fprintf(out,
        "Usage: %s [options]\n"
        "\n"
        "Standalone daemon exposing the libkatherine protocol emulator over real\n"
        "UDP sockets, so that an unmodified katherine_device_t client can talk to\n"
        "it as if it were a readout.\n"
        "\n"
        "Options:\n"
        "  --listen <addr>              local address to bind (default %s)\n"
        "  --ctl-port <port>            control port to bind (default %d)\n"
        "  --data-port <port>           data port to bind, i.e. this daemon's own\n"
        "                               source port for measurement data (default %d)\n"
        "  --client-data-port <port>    destination port for measurement data on\n"
        "                               the client host (default %d)\n"
        "  --profile <name>             emulated readout profile (only 'gen1-tpx3'\n"
        "                               is available)\n"
        "  --seed <n>                   seed of the pseudo-random generators\n"
        "  --rate <bytes/s>             measurement data rate limit, 0 to disable\n"
        "  --hits-per-frame <n>         pixel measurement data emitted per frame\n"
        "  --lost-per-frame <n>         lost hits reported per frame\n"
        "  --pattern <name>             uniform | hot-column | gradient\n"
        "  --ack-latency-us <n>         virtual latency of command responses\n"
        "  --drop-px-chunk <k>          drop the k-th 1024-byte pixel-configuration\n"
        "                               chunk instead of delivering it, 0 to disable\n"
        "  --stray-crd                  send one unsolicited response datagram right\n"
        "                               after the first command arrives\n"
        "  --log <file>                 append one line per received command to file\n"
        "  --quiet                      suppress the startup banner\n"
        "  --help                       print this message and exit\n",
        prog, DEFAULT_LISTEN_ADDR, DEFAULT_CTL_PORT, DEFAULT_DATA_PORT, DEFAULT_CLIENT_DATA_PORT);
}

static bool
parse_u64(const char *s, uint64_t *out)
{
    char *end;
    unsigned long long v;

    if (s == NULL || *s == '\0') return false;

    errno = 0;
    v     = strtoull(s, &end, 10);
    if (errno != 0 || *end != '\0') return false;

    *out = (uint64_t) v;
    return true;
}

static bool
parse_u32(const char *s, uint32_t *out)
{
    uint64_t v;

    if (!parse_u64(s, &v) || v > UINT32_MAX) return false;

    *out = (uint32_t) v;
    return true;
}

static bool
parse_port(const char *s, uint16_t *out)
{
    uint64_t v;

    if (!parse_u64(s, &v) || v > UINT16_MAX) return false;

    *out = (uint16_t) v;
    return true;
}

static bool
parse_pattern(const char *s, katherine_emu_pattern_t *out)
{
    if (strcmp(s, "uniform") == 0) {
        *out = KATHERINE_EMU_PATTERN_UNIFORM;
        return true;
    }
    if (strcmp(s, "hot-column") == 0) {
        *out = KATHERINE_EMU_PATTERN_HOT_COLUMN;
        return true;
    }
    if (strcmp(s, "gradient") == 0) {
        *out = KATHERINE_EMU_PATTERN_GRADIENT;
        return true;
    }
    return false;
}

/* Parses argv into *options, applying the defaults documented in print_usage().
 * Returns an exit code to use immediately (for --help and parse errors), or
 * -1 if the caller should proceed to run the daemon. */
static int
parse_options(int argc, char *argv[], daemon_options_t *options)
{
    int opt;
    const char *value;
    ksim_args_t args = {.index = 1};

    *options = (daemon_options_t) {
        .listen_addr      = DEFAULT_LISTEN_ADDR,
        .ctl_port         = DEFAULT_CTL_PORT,
        .data_port        = DEFAULT_DATA_PORT,
        .client_data_port = DEFAULT_CLIENT_DATA_PORT,
        .profile_name     = DEFAULT_PROFILE,
    };

    while ((opt = ksim_args_next(&args, argc, argv, OPTS, &value)) != -1) {
        switch (opt) {
        case OPT_LISTEN:
            options->listen_addr = value;
            break;

        case OPT_CTL_PORT:
            if (!parse_port(value, &options->ctl_port)) {
                fprintf(stderr, "ksim: invalid --ctl-port '%s'\n", value);
                return EXIT_FAILURE;
            }
            break;

        case OPT_DATA_PORT:
            if (!parse_port(value, &options->data_port)) {
                fprintf(stderr, "ksim: invalid --data-port '%s'\n", value);
                return EXIT_FAILURE;
            }
            break;

        case OPT_CLIENT_DATA_PORT:
            if (!parse_port(value, &options->client_data_port)) {
                fprintf(stderr, "ksim: invalid --client-data-port '%s'\n", value);
                return EXIT_FAILURE;
            }
            break;

        case OPT_PROFILE:
            options->profile_name = value;
            break;

        case OPT_SEED:
            if (!parse_u64(value, &options->seed)) {
                fprintf(stderr, "ksim: invalid --seed '%s'\n", value);
                return EXIT_FAILURE;
            }
            options->seed_set = true;
            break;

        case OPT_RATE:
            if (!parse_u64(value, &options->rate)) {
                fprintf(stderr, "ksim: invalid --rate '%s'\n", value);
                return EXIT_FAILURE;
            }
            options->rate_set = true;
            break;

        case OPT_HITS_PER_FRAME:
            if (!parse_u32(value, &options->hits_per_frame)) {
                fprintf(stderr, "ksim: invalid --hits-per-frame '%s'\n", value);
                return EXIT_FAILURE;
            }
            options->hits_set = true;
            break;

        case OPT_LOST_PER_FRAME:
            if (!parse_u32(value, &options->lost_per_frame)) {
                fprintf(stderr, "ksim: invalid --lost-per-frame '%s'\n", value);
                return EXIT_FAILURE;
            }
            options->lost_set = true;
            break;

        case OPT_PATTERN:
            if (!parse_pattern(value, &options->pattern)) {
                fprintf(stderr, "ksim: invalid --pattern '%s' (uniform | hot-column | gradient)\n", value);
                return EXIT_FAILURE;
            }
            options->pattern_set = true;
            break;

        case OPT_ACK_LATENCY_US:
            if (!parse_u64(value, &options->ack_latency_us)) {
                fprintf(stderr, "ksim: invalid --ack-latency-us '%s'\n", value);
                return EXIT_FAILURE;
            }
            options->ack_latency_set = true;
            break;

        case OPT_DROP_PX_CHUNK:
            if (!parse_u32(value, &options->drop_px_chunk)) {
                fprintf(stderr, "ksim: invalid --drop-px-chunk '%s'\n", value);
                return EXIT_FAILURE;
            }
            break;

        case OPT_STRAY_CRD:
            options->stray_crd = true;
            break;

        case OPT_LOG:
            options->log_path = value;
            break;

        case 'q':
            options->quiet = true;
            break;

        case 'h':
            print_usage(stdout, argv[0]);
            return EXIT_SUCCESS;

        default:
            print_usage(stderr, argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (strcmp(options->profile_name, "gen1-tpx3") != 0) {
        fprintf(stderr, "ksim: unsupported --profile '%s' (only 'gen1-tpx3' is available)\n",
            options->profile_name);
        return EXIT_FAILURE;
    }

    return -1;
}

/* ---------------------------------------------------------------------- */
/* Sockets and timing.                                                     */

/* Renders an IPv4 address as dotted-decimal text by hand, byte by byte, in
   preference to inet_ntop(): the address field is stored in network byte
   order regardless of host or platform endianness, so reading it through a
   byte pointer always yields the octets in their conventional left-to-right
   order, with no dependency on <arpa/inet.h> (POSIX) vs. <ws2tcpip.h>
   (Windows). */
static void
format_ipv4(struct in_addr addr, char *out, size_t out_size)
{
    unsigned char b[4];
    memcpy(b, &addr.s_addr, sizeof(b));
    snprintf(out, out_size, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
}

/* Repoints the data socket to the client's current address whenever the
   client's IP -- learned from the control socket's addr_remote, which
   katherine_udp_recv() keeps pointed at whoever sent last -- has changed.
   The client's data port is fixed by --client-data-port; only the host
   address can move. */
static void
track_client(katherine_udp_t *data_udp, const katherine_udp_t *ctl_udp, uint16_t client_data_port, bool *data_remote_set,
    struct in_addr *data_remote_ip, bool quiet)
{
    struct in_addr ip = ctl_udp->addr_remote.sin_addr;

    if (*data_remote_set && ip.s_addr == data_remote_ip->s_addr) return;

    char ip_str[16]; /* "255.255.255.255\0" */
    format_ipv4(ip, ip_str, sizeof(ip_str));

    int res = katherine_udp_set_remote(data_udp, ip_str, client_data_port);
    if (res != 0) {
        if (!quiet) {
            fprintf(stderr, "ksim: failed to repoint the data socket to %s:%u: %s\n", ip_str,
                (unsigned) client_data_port, strerror(res));
        }
        return;
    }

    *data_remote_ip  = ip;
    *data_remote_set = true;
}

/* Elapsed time from a monotonic sample taken at from to one taken at to (both
   ksim_monotonic_ns() results), clamped to zero (the caller always feeds
   this consecutive samples, so a negative result would only ever come from
   clock oddities, never from real elapsed time). */
static uint64_t
ns_diff(uint64_t from, uint64_t to)
{
    return to > from ? to - from : 0;
}

/* ---------------------------------------------------------------------- */
/* Signal handling.                                                        */

/* Set by the handler installed through ksim_install_stop_handler()
   (stopsig.h); checked by the main loop below. */
static volatile sig_atomic_t g_stop = 0;

/* ---------------------------------------------------------------------- */
/* Command log.                                                            */

static void
drain_log(FILE *log_fp, katherine_emu_t *emu)
{
    katherine_emu_log_entry_t entries[LOG_DRAIN_BATCH];
    size_t n;
    bool wrote = false;

    if (log_fp == NULL) return;

    while ((n = katherine_emu_log_read(emu, entries, LOG_DRAIN_BATCH)) > 0) {
        for (size_t i = 0; i < n; ++i) {
            fprintf(log_fp, "opcode=0x%02X sub=0x%02X payload=0x%08X\n", entries[i].opcode, entries[i].subindex,
                entries[i].payload);
        }
        wrote = true;
    }

    if (wrote) fflush(log_fp);
}

/* ---------------------------------------------------------------------- */
/* Main loop.                                                              */

/* Sends the stray CRD to whoever the control socket's addr_remote currently
   names -- i.e. the sender of the datagram that was just received, since
   katherine_udp_recv() keeps that field pointed at the last peer. */
static void
send_stray_crd(katherine_udp_t *ctl_udp, bool quiet)
{
    uint8_t crd[KATHERINE_EMU_CRD_SIZE] = {0};
    int res                             = katherine_udp_send_exact(ctl_udp, crd, sizeof(crd));

    if (quiet) return;
    if (res != 0) {
        fprintf(stderr, "ksim: failed to send the stray CRD: %s\n", strerror(res));
    } else {
        fprintf(stderr, "ksim: sent an unsolicited response datagram to the client\n");
    }
}

int
main(int argc, char *argv[])
{
    daemon_options_t options;
    int early_exit = parse_options(argc, argv, &options);
    if (early_exit >= 0) return early_exit;

    katherine_emu_profile_t profile;
    katherine_emu_profile_defaults(&profile);
    if (options.seed_set) profile.seed = options.seed;
    if (options.rate_set) profile.shape_bytes_per_s = options.rate;
    if (options.hits_set) profile.hits_per_frame = options.hits_per_frame;
    if (options.lost_set) profile.lost_per_frame = options.lost_per_frame;
    if (options.pattern_set) profile.pattern = options.pattern;
    if (options.ack_latency_set) profile.ack_latency_ns = options.ack_latency_us * 1000ull;

    katherine_emu_t emu;
    if (katherine_emu_init(&emu, &profile) != 0) {
        fprintf(stderr, "ksim: failed to initialize the emulator\n");
        return EXIT_FAILURE;
    }

    katherine_udp_t ctl_udp;
    int res = katherine_udp_init_bound(&ctl_udp, options.listen_addr, options.ctl_port, "0.0.0.0", 0, CTL_RECV_TIMEOUT_MS);
    if (res != 0) {
        fprintf(stderr, "ksim: cannot bind control socket on %s:%u: %s\n", options.listen_addr,
            (unsigned) options.ctl_port, strerror(res));
        katherine_emu_fini(&emu);
        return EXIT_FAILURE;
    }

    katherine_udp_t data_udp;
    res = katherine_udp_init_bound(&data_udp, options.listen_addr, options.data_port, "0.0.0.0", 0, 0);
    if (res != 0) {
        fprintf(stderr, "ksim: cannot bind data socket on %s:%u: %s\n", options.listen_addr,
            (unsigned) options.data_port, strerror(res));
        katherine_udp_fini(&ctl_udp);
        katherine_emu_fini(&emu);
        return EXIT_FAILURE;
    }

    FILE *log_fp = NULL;
    if (options.log_path != NULL) {
        log_fp = fopen(options.log_path, "a");
        if (log_fp == NULL) {
            fprintf(stderr, "ksim: cannot open --log file '%s': %s\n", options.log_path, strerror(errno));
            katherine_udp_fini(&data_udp);
            katherine_udp_fini(&ctl_udp);
            katherine_emu_fini(&emu);
            return EXIT_FAILURE;
        }
    }

    if (!options.quiet) {
        fprintf(stderr, "ksim: control %s:%u, data %s:%u -> client data port %u\n", options.listen_addr,
            (unsigned) options.ctl_port, options.listen_addr, (unsigned) options.data_port,
            (unsigned) options.client_data_port);
        fprintf(stderr, "ksim: profile=%s seed=%" PRIu64 "\n", options.profile_name, profile.seed);
    }

    res = ksim_install_stop_handler(&g_stop);
    if (res != 0) {
        fprintf(stderr, "ksim: cannot install the stop signal handler: %s\n", strerror(res));
        if (log_fp != NULL) fclose(log_fp);
        katherine_udp_fini(&data_udp);
        katherine_udp_fini(&ctl_udp);
        katherine_emu_fini(&emu);
        return EXIT_FAILURE;
    }

    bool client_known      = false;
    bool stray_crd_pending = options.stray_crd;

    bool data_remote_set = false;
    struct in_addr data_remote_ip;
    memset(&data_remote_ip, 0, sizeof(data_remote_ip));

    uint64_t commands_seen  = 0;
    uint64_t md_bytes_sent  = 0;
    uint64_t px_chunks_seen = 0;

    uint64_t prev_ns = ksim_monotonic_ns();

    while (!g_stop) {
        for (int i = 0; i < MAX_DRAIN_PER_TICK; ++i) {
            uint8_t buf[RECV_BUF_SIZE];
            size_t n = sizeof(buf);
            int rres = katherine_udp_recv(&ctl_udp, buf, &n);

            if (rres != 0) {
                if (rres == EAGAIN || rres == EWOULDBLOCK || rres == ETIMEDOUT) break;
                if (rres == EINTR) continue;
                fprintf(stderr, "ksim: recvfrom failed: %s\n", strerror(rres));
                break;
            }

            bool is_first_command = !client_known;
            client_known          = true;
            ++commands_seen;

            track_client(&data_udp, &ctl_udp, options.client_data_port, &data_remote_set, &data_remote_ip,
                options.quiet);

            bool deliver = true;
            if (n == PX_CHUNK_SIZE) {
                ++px_chunks_seen;
                if (options.drop_px_chunk != 0 && px_chunks_seen == options.drop_px_chunk) {
                    deliver = false;
                    if (!options.quiet) {
                        fprintf(stderr, "ksim: dropping pixel-config chunk #%" PRIu64 "\n", px_chunks_seen);
                    }
                }
            }

            if (deliver) (void) katherine_emu_cmd_in(&emu, buf, n);

            if (stray_crd_pending && is_first_command) {
                send_stray_crd(&ctl_udp, options.quiet);
                stray_crd_pending = false;
            }
        }

        uint64_t now_ns = ksim_monotonic_ns();
        katherine_emu_advance(&emu, ns_diff(prev_ns, now_ns));
        prev_ns = now_ns;

        if (client_known) {
            uint8_t crd[KATHERINE_EMU_CRD_SIZE];
            size_t crd_len;
            while (katherine_emu_crd_out(&emu, crd, &crd_len) == 0) {
                (void) katherine_udp_send_exact(&ctl_udp, crd, crd_len);
            }

            uint8_t md_buf[MD_DATAGRAM_MAX_BYTES];
            size_t md_len;
            while (katherine_emu_data_out(&emu, md_buf, sizeof(md_buf), &md_len) == 0) {
                if (katherine_udp_send_exact(&data_udp, md_buf, md_len) == 0) md_bytes_sent += (uint64_t) md_len;
            }
        }

        drain_log(log_fp, &emu);
    }

    drain_log(log_fp, &emu);
    if (log_fp != NULL) fclose(log_fp);

    katherine_udp_fini(&data_udp);
    katherine_udp_fini(&ctl_udp);

    fprintf(stderr,
        "ksim: summary: commands=%" PRIu64 " unknown=%" PRIu64 " dropped_crds=%" PRIu64
        " md_bytes=%" PRIu64 "\n",
        commands_seen, katherine_emu_unknown_cmd_count(&emu), katherine_emu_dropped_crd_count(&emu), md_bytes_sent);

    katherine_emu_fini(&emu);
    return EXIT_SUCCESS;
}
