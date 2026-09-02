/**
 * \file
 * \brief Recovery of a pixel-configuration upload whose acknowledgement is lost.
 *
 * The pixel matrix travels as a stateful exchange: the upload command, then
 * 64 raw chunks, then a single acknowledgement once the readout has counted
 * all 16384 configuration words. Nothing on the wire is reliable, so that
 * acknowledgement can be lost -- and when it is, the client cannot tell an
 * upload that failed midway from one that finished unheard, so it falls back
 * on the recovery the firmware prescribes: a flood of filler commands long
 * enough to walk the readout out of any upload state, followed by a retry.
 *
 * The readout's command dispatcher has no default branch, so the filler
 * flood itself provokes no responses (the emulator matches this: an
 * unrecognized opcode is only counted, never acknowledged). What the
 * recovery drain still has to guard against is whatever legitimate response
 * was already in flight when it started -- above all the very upload
 * acknowledgement this recovery was entered to replace, which the readout
 * may yet deliver after the client gave up waiting for it. This is the
 * hazard these cases pin down (issue #23, "config: drain stale responses
 * after px-config recovery"): a recovery that leaves such a response queued
 * at the client's control socket desynchronizes the session for good,
 * because from then on every command reads the response of an earlier one.
 * The two cases below are the two ways that shows:
 *
 *   - the responses left over are read as the answers to whatever comes
 *     next, so an inquiry no longer returns its own answer, and
 *   - the retry itself pairs with a stale response, so an upload that is
 *     *still* incomplete looks acknowledged: the readout stays in its
 *     upload state, swallows every subsequent command as configuration
 *     data, and the acquisition that follows never starts.
 *
 * The daemon is spawned from the path given in argv[1] and told which
 * datagrams to lose; the addressing, the ground truth of the data stream and
 * the environmental skip are those of test_e2e_acq.c, which documents them
 * at length.
 *
 * \author Petr Mánek
 * \date 21.8.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

// Must be the very first thing in the file, before any #include: kspawn.h
// guards its fork()/execv()/waitpid() declarations behind _POSIX_C_SOURCE,
// but that only has an effect the first time any libc header pulls in
// <features.h> -- glibc resolves POSIX visibility once for the whole
// translation unit and does not revisit it -- so it has to be set before
// literally anything else touches libc. Harmless on Windows, whose headers
// do not gate on it. Same reasoning as tools/ksim/main.c.
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// katherine/katherine.h must precede kspawn.h: on Windows it transitively
// pulls in <winsock2.h> (via udp_win.h) ahead of the <windows.h> that
// kspawn.h includes, and the reverse order does not compile under the
// Windows SDK.
#include <katherine/katherine.h>

#include "kspawn.h"
#include "ktest.h"
#include "msleep.h"

// ------------------------------------------------------------------
// Daemon parameters, as in test_e2e_acq.c: the numeric ones are needed both
// as text (for the daemon's command line) and as integers (for the
// expectations), so they are spelled once and stringified where a string is
// wanted.

#define KPX_STR_(x)       #x
#define KPX_STR(x)        KPX_STR_(x)

#define KSIM_LISTEN_ADDR  "127.0.0.2"
#define KSIM_SEED         12345
#define HITS_PER_FRAME    40
#define LOST_PER_FRAME    3

// Response datagram the daemon drops, counted from the first
// pixel-configuration upload command (--drop-crd in tools/ksim/main.c). The
// upload command itself is answered by nothing, and each of the 64 chunks
// that follow it is consumed as configuration data rather than as a
// command, so the first response of that count is the upload's own
// completion acknowledgement: the ordinal is 1 by construction, and needs
// no allowance for the readiness probing above, which is over before the
// count begins. Confirmed against the daemon's --log output, where the drop
// is reported between the last chunk of the upload and the matrix reset
// command that follows it.
#define DROP_DONE_ACK_CRD 1

// Chunk the daemon drops in the second case, counted over every 1024-byte
// datagram it receives (--drop-px-chunk): the 64 chunks of the first upload
// attempt, then the 3 x 64 filler datagrams of the recovery flood (also
// 1024 bytes each), then the chunks of the retry -- so the ordinal below
// falls inside the retry, leaving *it* incomplete as well. Any of the
// retry's 64 chunks would do, so the fourth is picked with room on either
// side: filler datagrams lost on the way (the flood is sent unpaced) shift
// the ordinal deeper into the retry, never out of it.
#define UPLOAD_CHUNKS     64
#define FLOOD_CHUNKS      (3 * UPLOAD_CHUNKS)
#define DROP_RETRY_CHUNK  (UPLOAD_CHUNKS + FLOOD_CHUNKS + 4)

// Readiness polling: the device's control timeout is 100 ms, so one failed
// attempt costs that much plus the sleep below -- 80 attempts bound the wait
// at roughly ten seconds.
#define READY_ATTEMPTS    80
#define READY_SLEEP_MS    25

// Chip identifier of the emulated readout, from
// katherine_emu_profile_defaults() in c/src/emu/emulator.c. A response
// belonging to another command decodes to something else, which is what
// makes this inquiry a test of the session rather than of the readout.
#define EXPECTED_CHIP_ID  "A1-W0001"

// Shutter short enough that the coarse time of arrival cannot wrap within a
// frame, as in test_e2e_acq.c.
#define SHORT_ACQ_TIME_NS 400000.0

// Acquisition buffers and timeouts, as in test_e2e_acq.c.
#define MD_BUFFER_SIZE    (KATHERINE_MD_SIZE * 4096)
#define PIXEL_BUFFER_HITS 1024
#define REPORT_TIMEOUT_MS 500
#define FAIL_TIMEOUT_MS   10000

// The acquisition mode under test, and the pixel layout it delivers.
typedef katherine_px_f_toa_tot_t px_t;

// ------------------------------------------------------------------
// Fixture: one daemon and one device per case, because the datagrams to
// lose are chosen on the daemon's command line and every case wants a
// readout that has not been talked to yet.

static kspawn_proc_t g_ksim = {0};
static katherine_device_t g_device;
static bool g_device_open = false;
static char g_error[256];

// Spawns the daemon, told to drop the completion acknowledgement of the
// first pixel-configuration upload and, if drop_chunk is nonzero, that
// 1024-byte datagram of the upload as well; then waits for the readout to
// answer a chip-identifier request with its own identifier. Returns NULL
// once it does, or a description of what went wrong.
//
// Readiness is the *content* of the answer, not merely a successful receive,
// and the device is torn down and recreated between attempts -- both for the
// reasons fixture_init() in test_e2e_acq.c documents: a command sent before
// the daemon has bound its address comes straight back to the library's own
// wildcard-bound control socket, which then reads it as the readout's
// acknowledgement and leaves the session addressed to itself for good.
static const char *
fixture_start(const char *ksim_path, uint32_t drop_chunk)
{
    char drop_chunk_arg[16];
    char *argv[] = {
        (char *) "ksim",
        (char *) "--listen",
        (char *) KSIM_LISTEN_ADDR,
        (char *) "--seed",
        (char *) KPX_STR(KSIM_SEED),
        (char *) "--hits-per-frame",
        (char *) KPX_STR(HITS_PER_FRAME),
        (char *) "--lost-per-frame",
        (char *) KPX_STR(LOST_PER_FRAME),
        (char *) "--pattern",
        (char *) "hot-column",
        (char *) "--drop-crd",
        (char *) KPX_STR(DROP_DONE_ACK_CRD),
        (char *) "--quiet",
        // Filled in below, or left as the terminator.
        NULL,
        NULL,
        NULL,
    };

    if (drop_chunk != 0) {
        size_t i = sizeof(argv) / sizeof(argv[0]) - 3;
        snprintf(drop_chunk_arg, sizeof(drop_chunk_arg), "%u", (unsigned) drop_chunk);
        argv[i]     = (char *) "--drop-px-chunk";
        argv[i + 1] = drop_chunk_arg;
    }

    int res = kspawn_start(&g_ksim, ksim_path, argv);
    if (res != 0) {
        snprintf(g_error, sizeof(g_error), "cannot spawn '%s': %s", ksim_path, strerror(res));
        return g_error;
    }

    char chip_id[KATHERINE_CHIP_ID_STR_SIZE];
    for (int attempt = 0; attempt < READY_ATTEMPTS; ++attempt) {
        res = katherine_device_init(&g_device, KSIM_LISTEN_ADDR);
        if (res != 0) {
            snprintf(g_error, sizeof(g_error), "cannot bind the local control/data ports 1555/1556: %s",
                katherine_strerror(res));
            return g_error;
        }
        g_device_open = true;

        if (katherine_get_chip_id(&g_device, chip_id) == 0 && strcmp(chip_id, EXPECTED_CHIP_ID) == 0) return NULL;

        katherine_device_fini(&g_device);
        g_device_open = false;

        if (!kspawn_alive(&g_ksim)) {
            snprintf(g_error, sizeof(g_error),
                "ksim exited during startup: it could not bind " KSIM_LISTEN_ADDR
                " (not aliased by default on macOS), or could not be executed at all");
            return g_error;
        }

        katherine_msleep(READY_SLEEP_MS);
    }

    snprintf(g_error, sizeof(g_error),
        "no answer from ksim at " KSIM_LISTEN_ADDR ":1555 within %d attempts: the local ports may be bound on the "
        "wildcard address by another process, or the wildcard/specific same-port coexistence this needs may be "
        "unsupported here (unproven on Windows)",
        READY_ATTEMPTS);
    return g_error;
}

// Releases the device and reaps the daemon. Safe to call at any point,
// however far fixture_start() got, and safe to call twice.
static void
fixture_stop(void)
{
    if (g_device_open) {
        katherine_device_fini(&g_device);
        g_device_open = false;
    }

    kspawn_stop(&g_ksim);
}

// ------------------------------------------------------------------
// Acquisition fixture. Only the hit count and the frame bookkeeping are of
// interest here: what the hits themselves carry is the subject of
// test_e2e_acq.c.

typedef struct acq_probe {
    uint32_t frames_started;
    uint32_t frames_ended;
    uint32_t frames_completed;
    uint64_t hits_delivered;
} acq_probe_t;

static void
on_frame_started(void *ctx, int frame_idx)
{
    acq_probe_t *probe = (acq_probe_t *) ctx;

    (void) frame_idx;
    ++probe->frames_started;
}

static void
on_pixels_received(void *ctx, const void *px, size_t count)
{
    acq_probe_t *probe = (acq_probe_t *) ctx;

    (void) px;
    probe->hits_delivered += count;
}

static void
on_frame_ended(void *ctx, int frame_idx, bool completed, const katherine_frame_info_t *info)
{
    acq_probe_t *probe = (acq_probe_t *) ctx;

    (void) frame_idx;
    ++probe->frames_ended;
    if (completed) ++probe->frames_completed;

    KT_CHECK_EQ(info->sent_pixels, HITS_PER_FRAME);
    KT_CHECK_EQ(info->received_pixels, HITS_PER_FRAME);
    KT_CHECK_EQ(info->lost_pixels, LOST_PER_FRAME);
}

// Fills in a configuration equivalent to the one c/examples/krun.c uses,
// less the pixel matrix: the emulated readout models the upload protocol
// (it counts the 16384 configuration words and acknowledges them) but not
// the matrix contents, so the zeroed matrix left by the memset below is
// uploaded and accepted like any other. Mirrors configure() in
// test_e2e_acq.c, whose acquisitions these have to compare against.
static void
configure(katherine_config_t *config, double acq_time_ns, int no_frames)
{
    memset(config, 0, sizeof(*config));

    config->bias_id   = 0;
    config->acq_time  = acq_time_ns;
    config->no_frames = no_frames;
    config->bias      = 230;

    config->phase = PHASE_1;
    config->freq  = FREQ_40;

    config->dacs.named.Ibias_Preamp_ON   = 128;
    config->dacs.named.Ibias_Preamp_OFF  = 8;
    config->dacs.named.VPReamp_NCAS      = 128;
    config->dacs.named.Ibias_Ikrum       = 15;
    config->dacs.named.Vfbk              = 164;
    config->dacs.named.Vthreshold_fine   = 476;
    config->dacs.named.Vthreshold_coarse = 8;
    config->dacs.named.Ibias_DiscS1_ON   = 100;
    config->dacs.named.Ibias_DiscS1_OFF  = 8;
    config->dacs.named.Ibias_DiscS2_ON   = 128;
    config->dacs.named.Ibias_DiscS2_OFF  = 8;
    config->dacs.named.Ibias_PixelDAC    = 128;
    config->dacs.named.Ibias_TPbufferIn  = 128;
    config->dacs.named.Ibias_TPbufferOut = 128;
    config->dacs.named.VTP_coarse        = 128;
    config->dacs.named.VTP_fine          = 256;
    config->dacs.named.Ibias_CP_PLL      = 128;
    config->dacs.named.PLL_Vcntrl        = 128;

    // The triggers, the delayed start and the test pulse generator are all
    // left disabled by the memset above.
}

// Runs one bounded data-driven acquisition of one frame and checks that it
// delivered the frame the daemon was told to produce. The pixel matrix is
// uploaded from inside katherine_acquisition_begin(), so this single call
// covers the lost acknowledgement, the recovery, the retry and the
// acquisition the session has to survive them for.
static void
check_one_frame(void)
{
    katherine_acquisition_t acq;
    acq_probe_t probe;
    katherine_config_t config;

    memset(&acq, 0, sizeof(acq));
    memset(&probe, 0, sizeof(probe));

    configure(&config, SHORT_ACQ_TIME_NS, 1);

    KT_REQUIRE(katherine_acquisition_init(&acq, &g_device, &probe, MD_BUFFER_SIZE, PIXEL_BUFFER_HITS * sizeof(px_t),
                   REPORT_TIMEOUT_MS, FAIL_TIMEOUT_MS)
        == 0);

    acq.handlers.frame_started   = on_frame_started;
    acq.handlers.frame_ended     = on_frame_ended;
    acq.handlers.pixels_received = on_pixels_received;

    // One frame only: katherine_acquisition_begin() rejects a data-driven
    // acquisition of more than one frame outright (-KATHERINE_E_INVAL).
    KT_CHECK_EQ(katherine_acquisition_begin(&acq, &config, READOUT_DATA_DRIVEN, ACQUISITION_MODE_TOA_TOT, true, true),
        0);
    KT_CHECK_EQ(katherine_acquisition_read(&acq), 0);

    KT_CHECK_EQ(acq.state, ACQUISITION_SUCCEEDED);
    KT_CHECK_EQ(acq.completed_frames, 1);
    KT_CHECK_EQ(acq.dropped_measurement_data, 0);

    KT_CHECK_EQ(probe.frames_started, 1);
    KT_CHECK_EQ(probe.frames_ended, 1);
    KT_CHECK_EQ(probe.frames_completed, 1);
    KT_CHECK_EQ(probe.hits_delivered, HITS_PER_FRAME);

    katherine_acquisition_fini(&acq);
}

// ------------------------------------------------------------------
// a) The upload's acknowledgement is lost, nothing else. The recovery and
// its retry put the readout back in a state where it takes commands, so the
// acquisition itself comes through; what must not survive the recovery is
// the queue of responses it provoked.

static void
test_acquisition_after_lost_upload_ack(void)
{
    check_one_frame();
}

static void
test_session_aligned_after_recovery(void)
{
    char chip_id[KATHERINE_CHIP_ID_STR_SIZE];

    KT_CHECK_EQ(katherine_get_chip_id(&g_device, chip_id), 0);
    KT_CHECK(strcmp(chip_id, EXPECTED_CHIP_ID) == 0);

    float temperature = 0.0f;
    KT_CHECK_EQ(katherine_get_readout_temperature(&g_device, &temperature), 0);
    KT_CHECK(temperature > 0.0f);
}

// ------------------------------------------------------------------
// b) The acknowledgement is lost and the retry loses a chunk of its own, as
// observed on a loaded CI runner. This is the fatal form: unless the
// recovery leaves the response queue empty, the retry's own wait pairs with
// a leftover response and reports success while the readout is still
// counting configuration words -- from where it swallows the rest of the
// session, the acquisition start included, and no measurement data ever
// arrives.
//
// The daemon is restarted for this case, both because the datagrams to lose
// are named on its command line and because the readout must not have been
// configured beforehand. Its startup can no longer be a reason to skip: the
// first case proved the environment can host it.

static const char *g_restart_error = NULL;

static void
test_acquisition_after_lost_ack_and_retry_chunk(void)
{
    KT_REQUIRE(g_restart_error == NULL);
    check_one_frame();
}

// ------------------------------------------------------------------

int
main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <path-to-ksim>\n", argv[0]);
        return 2;
    }

    const char *skip = fixture_start(argv[1], 0);
    if (skip != NULL) {
        printf("1..0 # SKIP %s\n", skip);
        fixture_stop();
        return 77;
    }

    // No KT_* macro ever exits the process -- KT_REQUIRE returns from the
    // current test at most -- so the daemon is stopped and reaped on every
    // path out of this function.
    KT_RUN(test_acquisition_after_lost_upload_ack);
    KT_RUN(test_session_aligned_after_recovery);
    fixture_stop();

    g_restart_error = fixture_start(argv[1], DROP_RETRY_CHUNK);
    if (g_restart_error != NULL) printf("# %s\n", g_restart_error);
    KT_RUN(test_acquisition_after_lost_ack_and_retry_chunk);
    fixture_stop();

    return kt_summary();
}
