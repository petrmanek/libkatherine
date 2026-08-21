/**
 * @file
 * @brief End-to-end acquisition tests against the ksim daemon.
 *
 * These tests drive the unmodified 1.x public API -- katherine_device_init(),
 * the katherine_get_* inquiries, katherine_acquisition_begin()/read()/abort()
 * -- over real UDP loopback sockets against ksim, the standalone daemon
 * hosting the protocol emulator (tools/ksim). The daemon is spawned by this
 * program from the path given in argv[1]; every assertion below compares what
 * the library delivers against ground truth the daemon was told to produce on
 * the command line, or against the emulated readout's documented profile
 * defaults.
 *
 * Addressing: device.c hard-codes the local ports 1555 (control) and 1556
 * (data) and binds them on the wildcard address, so the daemon cannot share
 * them on the same address; it is given the secondary loopback address
 * 127.0.0.2 instead, and the two coexist because both sides set
 * SO_REUSEADDR. That arrangement is a property of the host, not of the
 * library: where it does not hold (127.0.0.2 is not aliased by default on
 * macOS, and the wildcard/specific coexistence is unproven on Windows) the
 * daemon never becomes reachable and the whole program reports a TAP skip and
 * exits with 77, which CTest is configured to read as "skipped". Only the
 * daemon's own startup can skip: once it has answered a single command, every
 * later failure is a genuine protocol failure and fails the test.
 *
 * Ground truth (all of it read off c/src/emu/emu_stream.c):
 *   - --hits-per-frame is the number of pixel MDs per frame, and the count
 *     the frame-finished MD reports as sent (stage FINISHED emits
 *     n_sent = px_index, i.e. the hits actually emitted).
 *   - --lost-per-frame is the value of the single lost-pixel MD emitted per
 *     frame, when nonzero (stage LOST).
 *   - --pattern hot-column puts every hit of an acquisition in one column,
 *     drawn once from the seed, with the row equal to the index of the hit
 *     within its frame (draw_hit(), KATHERINE_EMU_PATTERN_HOT_COLUMN).
 *   - hits are spread evenly over the shutter: hit i falls at
 *     frame_len / (hits + 1) * (i + 1) after the frame opened, and its
 *     coarse ToA is that offset in 25 ns readout ticks, truncated to 14 bits
 *     (hit_due_ns(), draw_hit()).
 *   - the frame start and end timestamps are the frame's open and close
 *     instants in the same 25 ns ticks (stages START_* and END_*), so they
 *     differ by exactly the configured acquisition time.
 *
 * @author Petr Mánek
 * @date 21.8.26
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
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
#include <time.h>

// katherine/katherine.h must precede kspawn.h: on Windows it transitively
// pulls in <winsock2.h> (via udp_win.h) ahead of the <windows.h> that
// kspawn.h includes, and the reverse order does not compile under the
// Windows SDK.
#include <katherine/katherine.h>

#include "kspawn.h"
#include "ktest.h"
#include "msleep.h"

/* ------------------------------------------------------------------ */
/* Daemon parameters. The numeric ones are needed both as text (for the
   daemon's command line) and as integers (for the expectations), so they are
   spelled once and stringified where a string is wanted. */

#define E2E_STR_(x)          #x
#define E2E_STR(x)           E2E_STR_(x)

#define KSIM_LISTEN_ADDR     "127.0.0.2"
#define KSIM_SEED            12345
#define HITS_PER_FRAME       40
#define LOST_PER_FRAME       3

/* Readiness polling: the device's control timeout is 100 ms, so one failed
   attempt costs that much plus the sleep below -- 80 attempts bound the wait
   at roughly ten seconds. */
#define READY_ATTEMPTS       80
#define READY_SLEEP_MS       25

/* Emulated readout profile defaults, from katherine_emu_profile_defaults()
   in c/src/emu/emulator.c. */
#define EXPECTED_CHIP_ID     "A1-W0001"
#define EXPECTED_HW_TYPE     0x01
#define EXPECTED_HW_REVISION 0x01
#define EXPECTED_SERIAL      1
#define EXPECTED_FW_VERSION  1
#define EXPECTED_READOUT_T   30.0f
#define EXPECTED_SENSOR_T    40.0f

/* Communication status of the emulated readout: all eight data lines up, one
   sensor chip attached, and a reported data rate of zero because the daemon
   is started without --rate and therefore does not shape its stream
   (queue_comm_status() in c/src/emu/emulator.c). */
#define EXPECTED_COMM_LINES  0xFF
#define EXPECTED_DATA_RATE   0

/* The ADC channels answer a synthetic ramp of 0.125 V per channel, so a
   caller can tell them apart (CMD_TYPE_GET_ADC_VOLTAGE in emulator.c). */
#define ADC_CHANNEL          3
#define EXPECTED_ADC_VOLTAGE 0.5f

/* Period of the emulated readout timer, i.e. the unit of every timestamp in
   the measurement data stream (KATHERINE_EMU_TICK_NS in c/src/emu/emu.h). */
#define TICK_NS              25

/* Width of the coarse time-of-arrival field, in the emulator and in the
   pixel layouts of c/src/md.h alike. */
#define TOA_LIMIT            (1u << 14)

/* Shutter short enough that the coarse ToA cannot wrap within a frame: 0.4 ms
   is 16000 readout ticks, just inside the 16384 the field holds. That keeps
   the arrival times of a frame strictly increasing and leaves the timestamp
   offset the data-driven chain interleaves at zero, so the two chains under
   test below deliver the same ToA values. */
#define SHORT_ACQ_TIME_NS    400000.0

/* Long enough that the daemon keeps trickling hits out while the abort is
   issued and takes effect. */
#define LONG_ACQ_TIME_NS     200000000.0

/* Frames requested by the sequential-readout case. Data-driven acquisitions
   are limited to one frame by katherine_acquisition_begin() itself. */
#define SEQ_FRAMES           3

/* Deliberately not a local address: it must black-hole the datagram rather
   than have it delivered back to the sender. Every 127.0.0.0/8 address is
   local on Linux, so a command sent to (say) 127.0.0.9:1555 is delivered to
   the library's own wildcard-bound control socket and read back as if it
   were the readout's acknowledgement. 192.0.2.1 is TEST-NET-1 (RFC 5737),
   reserved for documentation and never assigned, so nothing answers. */
#define DEAD_ADDR            "192.0.2.1"

/* Deliberately the opposite of DEAD_ADDR: a local address nobody is bound
   to, so that a command sent there comes straight back to the sender (all
   of 127.0.0.0/8 is local) and exercises the faux-echo rejection. */
#define SILENT_LOOPBACK_ADDR "127.0.0.9"

/* Bounds the timeout case: the control timeout is 100 ms, and time(NULL) has
   one-second granularity, so anything at or under two seconds of difference
   proves the call did not hang. */
#define TIMEOUT_BUDGET_S     2.0

/* Acquisition buffers. The measurement data buffer only has to hold one
   datagram (the daemon sends at most 226 data at a time) with room to spare;
   the pixel buffer holds a whole frame, so that the hits of a frame reach
   the handler in one call. */
#define MD_BUFFER_SIZE       (KATHERINE_MD_SIZE * 4096)
#define PIXEL_BUFFER_HITS    1024
#define REPORT_TIMEOUT_MS    500
#define FAIL_TIMEOUT_MS      10000

/* The acquisition mode under test, and the pixel layout it delivers. */
typedef katherine_px_f_toa_tot_t px_t;

/* ------------------------------------------------------------------ */
/* Shared fixture: one daemon and one device for every case that talks to
   the emulated readout. Fixed UDP ports make the daemon a global resource,
   hence the RUN_SERIAL property on the CTest side and a single daemon here. */

static kspawn_proc_t g_ksim = {0};
static katherine_device_t g_device;
static char g_skip_reason[256];

/* Spawns the daemon and waits for it to answer a chip-identifier request with
   its own identifier. Returns NULL once the readout is reachable, or a
   description of why the environment cannot host it -- the only failure this
   program is allowed to treat as a skip.

   Readiness is the *content* of the answer, not merely a successful receive,
   and the device is torn down and recreated between attempts. Both follow
   from the same hazard: a command sent before the daemon has bound its
   address is not lost, because the whole of 127.0.0.0/8 is local, but
   delivered straight back to the library's own wildcard-bound control
   socket -- which then reads its own 8-byte command as the readout's
   acknowledgement. The device's sessions pin their remote address, so the
   echo cannot retarget them (it once could, and then no amount of retrying
   on the same device would recover); recreating the device between attempts
   still sheds anything else a failed attempt may have left queued. */
static const char *
fixture_init(const char *ksim_path)
{
    char *argv[] = {
        (char *) "ksim",
        (char *) "--listen",
        (char *) KSIM_LISTEN_ADDR,
        (char *) "--seed",
        (char *) E2E_STR(KSIM_SEED),
        (char *) "--hits-per-frame",
        (char *) E2E_STR(HITS_PER_FRAME),
        (char *) "--lost-per-frame",
        (char *) E2E_STR(LOST_PER_FRAME),
        (char *) "--pattern",
        (char *) "hot-column",
        (char *) "--quiet",
        NULL,
    };

    int res = kspawn_start(&g_ksim, ksim_path, argv);
    if (res != 0) {
        snprintf(g_skip_reason, sizeof(g_skip_reason), "cannot spawn '%s': %s", ksim_path, strerror(res));
        return g_skip_reason;
    }

    char chip_id[KATHERINE_CHIP_ID_STR_SIZE];
    for (int attempt = 0; attempt < READY_ATTEMPTS; ++attempt) {
        res = katherine_device_init(&g_device, KSIM_LISTEN_ADDR);
        if (res != 0) {
            snprintf(g_skip_reason, sizeof(g_skip_reason),
                "cannot bind the local control/data ports 1555/1556: %s", strerror(res));
            return g_skip_reason;
        }

        if (katherine_get_chip_id(&g_device, chip_id) == 0 && strcmp(chip_id, EXPECTED_CHIP_ID) == 0) return NULL;

        katherine_device_fini(&g_device);

        if (!kspawn_alive(&g_ksim)) {
            snprintf(g_skip_reason, sizeof(g_skip_reason),
                "ksim exited during startup: it could not bind " KSIM_LISTEN_ADDR
                " (not aliased by default on macOS), or could not be executed at all");
            return g_skip_reason;
        }

        katherine_msleep(READY_SLEEP_MS);
    }

    snprintf(g_skip_reason, sizeof(g_skip_reason),
        "no answer from ksim at " KSIM_LISTEN_ADDR ":1555 within %d attempts: the local ports may be bound on the "
        "wildcard address by another process, or the wildcard/specific same-port coexistence this needs may be "
        "unsupported here (unproven on Windows)",
        READY_ATTEMPTS);
    return g_skip_reason;
}

/* ------------------------------------------------------------------ */
/* b) Slow control round trips.                                        */

static void
test_slow_control(void)
{
    char chip_id[KATHERINE_CHIP_ID_STR_SIZE];
    KT_CHECK_EQ(katherine_get_chip_id(&g_device, chip_id), 0);
    KT_CHECK(strcmp(chip_id, EXPECTED_CHIP_ID) == 0);

    katherine_readout_status_t readout;
    KT_CHECK_EQ(katherine_get_readout_status(&g_device, &readout), 0);
    KT_CHECK_EQ(readout.hw_type, EXPECTED_HW_TYPE);
    KT_CHECK_EQ(readout.hw_revision, EXPECTED_HW_REVISION);
    KT_CHECK_EQ(readout.hw_serial_number, EXPECTED_SERIAL);
    KT_CHECK_EQ(readout.fw_version, EXPECTED_FW_VERSION);

    katherine_comm_status_t comm;
    KT_CHECK_EQ(katherine_get_comm_status(&g_device, &comm), 0);
    KT_CHECK_EQ(comm.comm_lines_mask, EXPECTED_COMM_LINES);
    KT_CHECK_EQ(comm.data_rate, EXPECTED_DATA_RATE);
    KT_CHECK(comm.chip_detected);

    /* The temperatures travel as IEEE-754 singles in the response payload
       and are reinterpreted, not converted, so they compare exactly. */
    float temperature = 0.0f;
    KT_CHECK_EQ(katherine_get_readout_temperature(&g_device, &temperature), 0);
    KT_CHECK(temperature == EXPECTED_READOUT_T);

    temperature = 0.0f;
    KT_CHECK_EQ(katherine_get_sensor_temperature(&g_device, &temperature), 0);
    KT_CHECK(temperature == EXPECTED_SENSOR_T);

    float voltage = 0.0f;
    KT_CHECK_EQ(katherine_get_adc_voltage(&g_device, ADC_CHANNEL, &voltage), 0);
    KT_CHECK(voltage == EXPECTED_ADC_VOLTAGE);

    /* The emulated readout reports all matrix patterns passing, which is
       what the library requires to accept the result. */
    KT_CHECK_EQ(katherine_perform_digital_test(&g_device), 0);
}

/* ------------------------------------------------------------------ */
/* Acquisition fixture shared by the remaining cases.                  */

typedef struct acq_probe {
    katherine_acquisition_t *acq; /* the running acquisition, for abort from a handler */

    uint32_t frame_ticks; /* expected end-minus-start timestamp difference */

    uint32_t frames_started;
    uint32_t frames_ended;
    uint32_t frames_completed;

    uint64_t hits_delivered; /* summed over every pixels_received call */
    uint32_t hits_in_frame;

    bool have_column; /* the hot column is drawn once per acquisition */
    uint8_t column;

    uint32_t bad_column; /* hits whose coordinates, ToA or ToT contradict */
    uint32_t bad_row;    /* the ground truth of the pattern; counted rather */
    uint32_t bad_toa;    /* than reported per hit, so one broken frame */
    uint32_t bad_tot;    /* cannot bury the summary in output */
    uint32_t bad_ftoa;
    uint64_t prev_toa;

    uint32_t data_callbacks; /* undecoded mode only */
    uint64_t data_bytes;
    bool abort_enabled; /* whether the abort cases below drive this run */
    bool abort_requested;
    int abort_result;
} acq_probe_t;

/* Aborts the acquisition in progress, once, if this run is one of the abort
   cases. Runs on the read loop's own thread, from inside the loop, and takes
   the control socket -- not the data socket the loop holds. */
static void
request_abort(acq_probe_t *probe)
{
    if (!probe->abort_enabled || probe->abort_requested) return;

    probe->abort_requested = true;
    probe->abort_result    = katherine_acquisition_abort(probe->acq);
}

static void
on_frame_started(void *ctx, int frame_idx)
{
    acq_probe_t *probe = (acq_probe_t *) ctx;

    KT_CHECK_EQ(frame_idx, probe->frames_started);

    ++probe->frames_started;
    probe->hits_in_frame = 0;
    probe->prev_toa      = 0;

    /* This is invoked by the new-frame datum, the first datum of the frame,
       so the shutter is open by the time it runs: aborting here aborts a
       frame in progress. It is the decoding chain's counterpart of the abort
       from on_data_received below, which decoding never calls. */
    request_abort(probe);
}

static void
on_pixels_received(void *ctx, const void *px, size_t count)
{
    acq_probe_t *probe = (acq_probe_t *) ctx;
    const px_t *hits   = (const px_t *) px;

    probe->hits_delivered += count;

    for (size_t i = 0; i < count; ++i) {
        if (!probe->have_column) {
            probe->column      = hits[i].coord.x;
            probe->have_column = true;
        } else if (hits[i].coord.x != probe->column) {
            ++probe->bad_column;
        }

        if (hits[i].coord.y != (uint8_t) probe->hits_in_frame) ++probe->bad_row;

        /* Strictly increasing within a frame, and inside the 14-bit field:
           the arrival times are spread evenly over a shutter chosen short
           enough not to wrap the field, so any offset misapplied on top of
           them shows up here. */
        if (hits[i].toa >= TOA_LIMIT) ++probe->bad_toa;
        if (probe->hits_in_frame > 0 && hits[i].toa <= probe->prev_toa) ++probe->bad_toa;

        if (hits[i].tot > 1023) ++probe->bad_tot;
        if (hits[i].ftoa > 15) ++probe->bad_ftoa;

        probe->prev_toa = hits[i].toa;
        ++probe->hits_in_frame;
    }
}

static void
on_frame_ended(void *ctx, int frame_idx, bool completed, const katherine_frame_info_t *info)
{
    acq_probe_t *probe = (acq_probe_t *) ctx;

    KT_CHECK_EQ(frame_idx, probe->frames_ended);
    ++probe->frames_ended;
    if (completed) ++probe->frames_completed;

    /* A frame an abort cut short carries neither the frame-finished datum
       nor the end timestamp, so how many hits it was to deliver and when it
       was to close are not knowable: only a frame that ran to its end is
       held against the ground truth below. */
    if (!completed) return;

    KT_CHECK_EQ(info->sent_pixels, HITS_PER_FRAME);
    KT_CHECK_EQ(info->received_pixels, HITS_PER_FRAME);
    KT_CHECK_EQ(info->lost_pixels, LOST_PER_FRAME);
    KT_CHECK_EQ(probe->hits_in_frame, HITS_PER_FRAME);

    /* KNOWN-BUG (issue #23, "acq: fix frame timestamp word order"): the
       composite katherine_frame_info_time_t
       .d member is unusable, because the two halves are declared in the
       opposite order to the one the readout sends them in, so .d reads as
       (lsb << 32) | msb. Only the raw halves are asserted here, per current
       behavior; the .d expectations are added when the field order is fixed.

       The halves themselves are a wall-clock quantity -- the frame opens at
       whatever the daemon's virtual clock reads -- so what is pinned down is
       their relation: a frame lasts exactly the configured shutter, in 25 ns
       ticks, and the daemon has not been alive for the 107 seconds it would
       take the low half to overflow. */
    KT_CHECK_EQ(info->start_time.b.msb, 0);
    KT_CHECK_EQ(info->end_time.b.msb, 0);
    KT_CHECK_EQ((uint32_t) (info->end_time.b.lsb - info->start_time.b.lsb), probe->frame_ticks);
}

static void
on_data_received(void *ctx, const char *data, size_t count)
{
    acq_probe_t *probe = (acq_probe_t *) ctx;

    (void) data;

    ++probe->data_callbacks;
    probe->data_bytes += count;
    KT_CHECK_EQ(count % KATHERINE_MD_SIZE, 0);

    /* The first datagram of a frame necessarily opens with the new-frame
       datum, so the shutter is open by the time this runs: aborting here
       aborts a frame in progress. */
    request_abort(probe);
}

/* Fills in a configuration equivalent to the one c/examples/krun.c uses,
   less the pixel matrix: the emulated readout models the upload protocol
   (it counts the 16384 configuration words and acknowledges them) but not
   the matrix contents, so the zeroed matrix left by the memset below is
   uploaded and accepted like any other. */
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

    /* The triggers, the delayed start and the test pulse generator are all
       left disabled by the memset above. */
}

/* Configures the readout, runs one bounded acquisition to completion and
   returns what katherine_acquisition_read() returned. The acquisition is
   left initialized (and zeroed beforehand, so finalizing it is safe on
   every path) for the caller to inspect and finalize. With abort_enabled,
   the run is instead cut short from inside the read loop, by the first
   handler of the selected chain that reports the shutter open. */
static int
acq_run(katherine_acquisition_t *acq, acq_probe_t *probe, char readout_mode, double acq_time_ns, int no_frames,
    bool decode_data, bool abort_enabled)
{
    katherine_config_t config;
    int res;

    memset(acq, 0, sizeof(*acq));
    memset(probe, 0, sizeof(*probe));
    probe->acq           = acq;
    probe->frame_ticks   = (uint32_t) (acq_time_ns / TICK_NS);
    probe->abort_enabled = abort_enabled;

    configure(&config, acq_time_ns, no_frames);

    res = katherine_acquisition_init(
        acq, &g_device, probe, MD_BUFFER_SIZE, PIXEL_BUFFER_HITS * sizeof(px_t), REPORT_TIMEOUT_MS, FAIL_TIMEOUT_MS);
    if (res != 0) return res;

    acq->handlers.frame_started   = on_frame_started;
    acq->handlers.frame_ended     = on_frame_ended;
    acq->handlers.pixels_received = on_pixels_received;
    acq->handlers.data_received   = on_data_received;

    res = katherine_acquisition_begin(acq, &config, readout_mode, ACQUISITION_MODE_TOA_TOT, true, decode_data);
    if (res != 0) return res;

    return katherine_acquisition_read(acq);
}

/* ------------------------------------------------------------------ */
/* c) Data-driven acquisition against the daemon's ground truth.       */

static void
test_data_driven_frame(void)
{
    katherine_acquisition_t acq;
    acq_probe_t probe;

    /* One frame only: katherine_acquisition_begin() rejects a data-driven
       acquisition of more than one frame outright (EINVAL), which is what
       bounds this case. */
    KT_CHECK_EQ(acq_run(&acq, &probe, READOUT_DATA_DRIVEN, SHORT_ACQ_TIME_NS, 1, true, false), 0);

    KT_CHECK_EQ(acq.state, ACQUISITION_SUCCEEDED);
    KT_CHECK_EQ(acq.completed_frames, 1);
    KT_CHECK_EQ(acq.dropped_measurement_data, 0);
    KT_CHECK(!acq.aborted);

    KT_CHECK_EQ(probe.frames_started, 1);
    KT_CHECK_EQ(probe.frames_ended, 1);
    KT_CHECK_EQ(probe.frames_completed, 1);
    KT_CHECK_EQ(probe.hits_delivered, HITS_PER_FRAME);

    KT_CHECK(probe.have_column);
    KT_CHECK_EQ(probe.bad_column, 0);
    KT_CHECK_EQ(probe.bad_row, 0);
    KT_CHECK_EQ(probe.bad_toa, 0);
    KT_CHECK_EQ(probe.bad_tot, 0);
    KT_CHECK_EQ(probe.bad_ftoa, 0);

    katherine_acquisition_fini(&acq);
}

/* ------------------------------------------------------------------ */
/* c') The same ground truth over several frames. Sequential readout is
   the only mode the 1.x API lets a caller bound by a frame count above
   one; the emulated readout serves it from the same generator, minus the
   timestamp-offset data the data-driven chain interleaves.              */

static void
test_sequential_frames(void)
{
    katherine_acquisition_t acq;
    acq_probe_t probe;

    KT_CHECK_EQ(acq_run(&acq, &probe, READOUT_SEQUENTIAL, SHORT_ACQ_TIME_NS, SEQ_FRAMES, true, false), 0);

    KT_CHECK_EQ(acq.state, ACQUISITION_SUCCEEDED);
    KT_CHECK_EQ(acq.completed_frames, SEQ_FRAMES);
    KT_CHECK_EQ(acq.dropped_measurement_data, 0);

    KT_CHECK_EQ(probe.frames_started, SEQ_FRAMES);
    KT_CHECK_EQ(probe.frames_ended, SEQ_FRAMES);
    KT_CHECK_EQ(probe.frames_completed, SEQ_FRAMES);
    KT_CHECK_EQ(probe.hits_delivered, SEQ_FRAMES * HITS_PER_FRAME);

    KT_CHECK_EQ(probe.bad_column, 0);
    KT_CHECK_EQ(probe.bad_row, 0);
    KT_CHECK_EQ(probe.bad_toa, 0);
    KT_CHECK_EQ(probe.bad_tot, 0);
    KT_CHECK_EQ(probe.bad_ftoa, 0);

    katherine_acquisition_fini(&acq);
}

/* ------------------------------------------------------------------ */
/* d) Abort path. Once the abort is requested, the read loop drains
   whatever the readout has already sent -- including the aborted
   measurement datum that closes the interrupted frame -- and ends the
   acquisition as soon as the stream dries up. That is one behavior, not
   two: the pair of cases below differ only in which chain carries the
   data, and assert the same outcome.                                    */

static void
test_abort_undecoded(void)
{
    katherine_acquisition_t acq;
    acq_probe_t probe;

    KT_CHECK_EQ(acq_run(&acq, &probe, READOUT_DATA_DRIVEN, LONG_ACQ_TIME_NS, 1, false, true), 0);

    KT_CHECK(probe.data_callbacks > 0);
    KT_CHECK(probe.abort_requested);
    KT_CHECK_EQ(probe.abort_result, 0);

    KT_CHECK(acq.aborted);
    KT_CHECK_EQ(acq.state, ACQUISITION_SUCCEEDED);

    /* The shutter was still open, so no frame ever finished; and with
       decoding off, no frame ever started as far as the library is
       concerned either. */
    KT_CHECK_EQ(acq.completed_frames, 0);
    KT_CHECK_EQ(probe.frames_started, 0);
    KT_CHECK_EQ(probe.frames_ended, 0);

    katherine_acquisition_fini(&acq);
}

static void
test_abort_decoded(void)
{
    katherine_acquisition_t acq;
    acq_probe_t probe;

    KT_CHECK_EQ(acq_run(&acq, &probe, READOUT_DATA_DRIVEN, LONG_ACQ_TIME_NS, 1, true, true), 0);

    /* The undecoded chain is not consulted at all here: the abort came from
       the frame-started handler instead. */
    KT_CHECK_EQ(probe.data_callbacks, 0);
    KT_CHECK(probe.abort_requested);
    KT_CHECK_EQ(probe.abort_result, 0);

    KT_CHECK(acq.aborted);
    KT_CHECK_EQ(acq.state, ACQUISITION_SUCCEEDED);

    /* The shutter was still open, so the frame the abort interrupted is
       reported ended and incomplete, and it delivered fewer hits than a
       whole frame carries. */
    KT_CHECK_EQ(acq.completed_frames, 0);
    KT_CHECK_EQ(probe.frames_started, 1);
    KT_CHECK_EQ(probe.frames_ended, 1);
    KT_CHECK_EQ(probe.frames_completed, 0);
    KT_CHECK(probe.hits_delivered < HITS_PER_FRAME);

    katherine_acquisition_fini(&acq);
}

/* ------------------------------------------------------------------ */
/* e) Timeout path. Runs after the shared device has been finalized and
   the daemon stopped, so this device has the local ports to itself and
   no late response from the emulated readout can be mistaken for one.   */

static void
test_control_timeout(void)
{
    katherine_device_t device;
    KT_REQUIRE(katherine_device_init(&device, DEAD_ADDR) == 0);

    char chip_id[KATHERINE_CHIP_ID_STR_SIZE];
    time_t started = time(NULL);
    int res        = katherine_get_chip_id(&device, chip_id);
    double elapsed = difftime(time(NULL), started);

    /* Any nonzero code counts: a receive timeout on POSIX, but possibly
       WSAECONNRESET on Windows, where the ICMP port-unreachable of a
       refusing host is reported on the socket that provoked it, or an
       immediate send failure on a host with no route to the address. */
    KT_CHECK(res != 0);
    KT_CHECK(elapsed <= TIMEOUT_BUDGET_S);

    katherine_device_fini(&device);
}

/* ------------------------------------------------------------------ */
/* f) Faux-echo path. A loopback address nobody is bound to delivers a
   command straight back to the library's own wildcard-bound control
   socket, where it parses as a response with all identifier fields
   zeroed -- once mistaken for a genuine answer, discovery over an
   address range reported a phantom readout "@0-W0000" on the host's
   own addresses. The library must treat it like a timeout. Where the
   platform does not deliver to self, the inquiry fails anyway (timeout
   or no route), so this asserts the same outcome either way.           */

static void
test_faux_echo_rejected(void)
{
    katherine_device_t device;
    KT_REQUIRE(katherine_device_init(&device, SILENT_LOOPBACK_ADDR) == 0);

    char chip_id[KATHERINE_CHIP_ID_STR_SIZE];
    KT_CHECK(katherine_get_chip_id(&device, chip_id) != 0);

    katherine_device_fini(&device);
}

/* ------------------------------------------------------------------ */

int
main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <path-to-ksim>\n", argv[0]);
        return 2;
    }

    const char *skip = fixture_init(argv[1]);
    if (skip != NULL) {
        printf("1..0 # SKIP %s\n", skip);
        kspawn_stop(&g_ksim);
        return 77;
    }

    /* No KT_* macro ever exits the process -- KT_REQUIRE returns from the
       current test at most -- so the daemon is stopped and reaped on every
       path out of this function. */
    KT_RUN(test_slow_control);
    KT_RUN(test_data_driven_frame);
    KT_RUN(test_sequential_frames);
    KT_RUN(test_abort_undecoded);
    KT_RUN(test_abort_decoded);

    katherine_device_fini(&g_device);
    kspawn_stop(&g_ksim);

    KT_RUN(test_control_timeout);
    KT_RUN(test_faux_echo_rejected);

    return kt_summary();
}
