/**
 * \file
 * \brief Functions related to the data acquisition process.
 * \author Petr Mánek
 * \date 29.5.18
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <katherine/global.h>
#include <katherine/error.h>
#include <katherine/device.h>
#include <katherine/config.h>
#include <katherine/px.h>

/**
 * \defgroup katherine_acquisition Acquisition
 * \ingroup katherine_c_api
 * \brief Running an acquisition and decoding the measurement data it returns.
 */

/**
 * \addtogroup katherine_acquisition
 * \{
 */

/// Bytes in one measurement datum on the wire.
#define KATHERINE_MD_SIZE 6

// Uncomment the following line to enable acquisition logging:
// #define KATHERINE_DEBUG_ACQ 2

#ifdef __cplusplus
extern "C" {
#endif

/** A readout timestamp in the two halves the protocol sends it as. */
typedef struct katherine_frame_info_time_split {
    // The least significant half is declared first so that the union below
    // composes correctly on a little-endian host: the low bytes of d and
    // the first member share the same addresses. (Declared the other way
    // around, d read as (lsb << 32) | msb.)
    uint32_t lsb; ///< Low half, as its own measurement datum carried it.
    uint32_t msb; ///< High half, as its own measurement datum carried it.
} katherine_frame_info_time_split_t;

/** The same timestamp read either as its two halves or as one number. */
typedef union katherine_frame_info_time {
    katherine_frame_info_time_split_t b; ///< The halves, as received.
    uint64_t d;                          ///< The whole, composed.
} katherine_frame_info_time_t;

KATHERINE_EXPORTED int
katherine_frame_info_time_snprint(char *buf, size_t cap, const katherine_frame_info_time_t *v);

/** What the readout reported about one frame, and what libkatherine saw of it. */
typedef struct katherine_frame_info {
    uint64_t received_pixels; ///< The number of hit pixels actually received by libkatherine
    uint64_t sent_pixels;     ///< The number of hit pixels reported sent by Katherine device
    uint64_t lost_pixels;     ///< The number of hit pixels reported lost by Katherine device

    katherine_frame_info_time_t start_time; ///< Timestamp of frame start reported by Katherine device
    katherine_frame_info_time_t end_time;   ///< Timestamp of frame end, only valid after the frame has ended

    time_t start_time_observed; ///< Timestamp of when libkatherine received 'frame started' event, in local time reference
    time_t end_time_observed;   ///< Timestamp of when libkatherine received 'frame ended' event, only valid after the frame has ended

    bool completed; ///< Set to true if the frame was correctly terminated ahead of the 'frame ended' event. Otherwise this indicates missing data.
} katherine_frame_info_t;

KATHERINE_EXPORTED int
katherine_frame_info_snprint(char *buf, size_t cap, const katherine_frame_info_t *v);

/**
 * Callbacks an acquisition may invoke. Each is optional: unset handlers are
 * cleared by katherine_acquisition_init() and skipped when they would be
 * called, so a caller registers only what it wants.
 *
 * Which ones can fire is decided by katherine_acquisition_t::decode_data, and
 * the two sets are disjoint -- the first three belong to the decoded path and
 * the last to the raw one.
 */
typedef struct katherine_acquisition_handlers {
    // If katherine_acquisition_t::decode_data == true, you may register
    // handlers for the following callbacks:
    void (*pixels_received)(void *, const void *, size_t);                  ///< Decoded pixels, in batches
    void (*frame_started)(void *, int);                                     ///< New frame opened
    void (*frame_ended)(void *, int, bool, const katherine_frame_info_t *); ///< Current frame closed, with its info

    // If katherine_acquisition_t::decode_data == false, you may register
    // handlers for the following callbacks:
    void (*data_received)(void *, const char *, size_t); ///< Raw, undecoded measurement data as received
} katherine_acquisition_handlers_t;

/**
 * How the sensor moves hits out of the matrix.
 *
 * \internal
 * 0 = sequential, 1 = data-driven is the wire truth (readout manual sec.
 * 1.2.17, the argument of CMD_TYPE_SEQ_READOUT_START): at least one mature
 * client implementation of this protocol inverts its own internal enum
 * (data-driven = 0) and compensates for it when encoding the command. Do
 * not "fix" this enum to match such an implementation; its values already
 * match the wire directly.
 */
typedef enum katherine_tpx3_readout_mode {
    KATHERINE_TPX3_READOUT_SEQUENTIAL  = 0, ///< Frame-based mode: hits are sequentially read out from the matrix at the end of each frame, potentially resulting in dead time between frames. At most the entire matrix (65k pixels) can be hit.
    KATHERINE_TPX3_READOUT_DATA_DRIVEN = 1  ///< Data-driven mode: hits are propagated through super-pixels while the measurement is ongoing, causing local (but importantly, not global) dead time. Be careful in noisy or data-intensive environments, this mode can produce a lot of data (up to 40 Mhit/s).
} katherine_tpx3_readout_mode_t;

KATHERINE_EXPORTED const char *
katherine_str_readout_mode(katherine_tpx3_readout_mode_t mode);

/** Where an acquisition has got to; read it from katherine_acquisition_t::state. */
typedef enum katherine_acquisition_state {
    KATHERINE_ACQUISITION_STATE_NOT_STARTED = 0, ///< The detector is not sensitive, call katherine_acquisition_begin to start measurement.
    KATHERINE_ACQUISITION_STATE_RUNNING     = 1, ///< The detector is sensitive and collecting data, call katherine_acquisition_read() to retrieve its output or katherine_acquisition_{stop,abort}() to interrupt it. Note that for decode_data == false, only katherine_acquisition_abort() is viable.
    KATHERINE_ACQUISITION_STATE_SUCCEEDED   = 2, ///< The detector is no longer sensitive, the last measurement data arrived and the measurement concluded in an orderly manner.
    KATHERINE_ACQUISITION_STATE_TIMED_OUT   = 3, ///< Communications timeout. Either the detector is still measuring and the data flow was disrupted, or the device was powered off unexpectedly. This is also a common failure mode if decode_data == false, and the user fails to call katherine_acquisition_abort() at the end of the measurement.
} katherine_acquisition_state_t;

/**
 * What became of katherine_config_t::correct_phase for a given
 * acquisition. Three outcomes, not two: a device that never staggers its
 * columns and one that corrects the stagger itself are different situations,
 * and only the second leaves an offset that has to be undone to recover the
 * sensor's own counters.
 */
typedef enum katherine_phase_correction {
    KATHERINE_PHASE_CORRECTION_NONE     = 0, ///< Not requested, or nothing to correct.
    KATHERINE_PHASE_CORRECTION_SOFTWARE = 1, ///< Applied by the decoder.
    KATHERINE_PHASE_CORRECTION_HARDWARE = 2, ///< Offloaded to the readout.
} katherine_phase_correction_t;

KATHERINE_EXPORTED const char *
katherine_str_phase_correction(katherine_phase_correction_t v);

/**
 * One acquisition: the device it runs on, what was asked of it, the buffers it
 * decodes through and the counters it fills in.
 *
 * Allocated by the caller and initialized by katherine_acquisition_init(), so
 * every member is visible. Only the handlers and the counters are a caller's
 * business; the rest is the read loop's state and is documented as such.
 */
typedef struct katherine_acquisition {
    katherine_device_t *device; ///< Device this acquisition runs on, as passed to katherine_acquisition_init().
    void *user_ctx;             ///< The caller's context, handed back to every handler untouched.

    katherine_acquisition_state_t state; ///< Where the acquisition has got to.
    bool aborted;                        ///< Internal: true once an abort has been requested, which is how the read loop tells a dried-up stream from a lost one.

    // PINNED for the ASIC work (Petr, 2026-09-04): nothing in an acquisition
    // should be chip-type aware, and these two are Timepix3's. They belong in
    // the device's own record of what it last configured -- written by
    // katherine_configure(), read back by katherine_acquisition_begin() --
    // which is where the configuration restructure puts them. Left here until
    // Timepix2 or Timepix4 lands and forces the issue.
    katherine_tpx3_readout_mode_t readout_mode; ///< Readout mode in force, chosen at katherine_acquisition_begin().
    katherine_tpx3_px_mode_t px_mode;           ///< Pixel mode in force, which decides the hit type the handlers receive.
    bool fast_vco_enabled;                      ///< Whether the fast oscillator is on, which decides the fast hit variant.

    char *md_buffer;       ///< Buffer for a single incoming datagram containing MD, allocated by katherine_acquisition_init().
    size_t md_buffer_size; ///< Capacity of md_buffer in bytes. Must exceed the largest datagram a readout can send.

    /**
     * Whether measurement data is decoded into pixels, chosen at
     * katherine_acquisition_begin().
     *
     * True is the ordinary path: data is decoded, the frame lifecycle is
     * tracked, and pixels_received, frame_started and frame_ended are the
     * callbacks that fire. The acquisition ends by itself once all the
     * requested frames have arrived (or an error such as a communications
     * timeout is detected).
     *
     * False hands the raw binary measurement data to data_received and
     * interprets none of it. Nothing is decoded, so the other three callbacks
     * never fire, frame_info is never populated with valid data, and nothing
     * ends the acquisition on its own (other than timeout). The frame-finished
     * datum, which would normally stop it, is not decoded nor acted upon. This
     * shifts the responsibility to the user, who needs to call
     * katherine_acquisition_abort() to end the acquisition in an orderly
     * manner.
     */
    bool decode_data;
    char *pixel_buffer;            ///< Decoded hits awaiting delivery, allocated by katherine_acquisition_init().
    size_t pixel_buffer_size;      ///< Capacity of pixel_buffer in bytes.
    size_t pixel_buffer_valid;     ///< Internal: hits currently in the buffer.
    size_t pixel_buffer_max_valid; ///< Internal: hits that fit, which is the batch size a handler is given.

    int requested_frames;            ///< Frames asked for at katherine_acquisition_begin().
    double requested_frame_duration; ///< Requested duration of a single frame, in seconds
    int completed_frames;            ///< Frames finished so far.
    size_t dropped_measurement_data; ///< Number of MD words the decoder did not recognise since acquisition start.

    /**
     * Datagrams received that exactly filled md_buffer_size, the portable
     * heuristic for a receive that may have silently truncated a longer
     * datagram (see katherine_acquisition_init()). A datagram this size can
     * overcount, so this is a signal to raise md_buffer_size, not an exact
     * loss count. Reset by katherine_acquisition_begin(), like
     * dropped_measurement_data.
     */
    uint64_t truncated_measurement_data;

    time_t acq_start_time; ///< Internal: when the acquisition began, as time() reports it.
    int report_timeout;    ///< Milliseconds of silence from the readout after which a partly filled pixel buffer is delivered; zero disables.
    int fail_timeout;      ///< Milliseconds allowed past the requested measurement's own duration before the acquisition is declared timed out; zero disables.

    katherine_acquisition_handlers_t handlers; ///< The callbacks; set them after katherine_acquisition_init() and before beginning.
    katherine_frame_info_t current_frame_info; ///< Internal: the frame being filled, handed to frame_ended when it closes.

    /**
     * Timestamp offset in effect, in fine-oscillator ticks. Carries the epoch
     * bias described at katherine_px_f_toa_tot_t::timestamp, so it is never
     * zero during an acquisition and is not the offset the stream delivered.
     */
    uint64_t last_toa_offset;

    /**
     * Shift taking a coarse tick to the fine ticks that make it up, resolved
     * by katherine_acquisition_begin(). The ratio itself is 1 << this.
     */
    uint8_t toa_coarse_tick_to_fine_shift;

    /** What the phase request resolved to, decided by katherine_acquisition_begin(). */
    katherine_phase_correction_t phase_correction;

    /**
     * Pixel-clock phases this configuration actually yields, which the clock
     * divider may clamp below what katherine_tpx3_phase_t asked for.
     */
    uint8_t phase_count;

    /**
     * Per-column phase offsets in fine-oscillator ticks, added by the decoder.
     * Filled only when phase_correction is SOFTWARE; all zeroes otherwise, so
     * the decoder needs no test for whether correction is in effect. Read it
     * through katherine_acquisition_timestamp_phase_offset().
     */
    uint8_t phase_offsets[KATHERINE_TPX3_MATRIX_WIDTH];

    bool frame_active; ///< Internal: whether a frame is currently open.
} katherine_acquisition_t;

KATHERINE_EXPORTED int
katherine_acquisition_snprint(char *buf, size_t cap, const katherine_acquisition_t *v);

KATHERINE_EXPORTED uint8_t
katherine_acquisition_timestamp_phase_offset(const katherine_acquisition_t *acq, katherine_tpx3_coord_t coord);

KATHERINE_EXPORTED katherine_error_t
katherine_acquisition_init(katherine_acquisition_t *acq, katherine_device_t *device, void *ctx, size_t md_buffer_size, size_t pixel_buffer_size, int report_timeout, int fail_timeout);

KATHERINE_EXPORTED void
katherine_acquisition_fini(katherine_acquisition_t *acq);

KATHERINE_EXPORTED katherine_error_t
katherine_acquisition_begin(katherine_acquisition_t *acq, const katherine_config_t *config, katherine_tpx3_readout_mode_t readout_mode, katherine_tpx3_px_mode_t px_mode, bool fast_vco_enabled, bool decode_data);

KATHERINE_EXPORTED katherine_error_t
katherine_acquisition_abort(katherine_acquisition_t *acq);

KATHERINE_EXPORTED katherine_error_t
katherine_acquisition_stop(katherine_acquisition_t *acq);

KATHERINE_EXPORTED katherine_error_t
katherine_acquisition_read(katherine_acquisition_t *acq);

KATHERINE_EXPORTED const char *
katherine_str_acquisition_state(katherine_acquisition_state_t state);

#ifdef __cplusplus
}
#endif

/** \} */
