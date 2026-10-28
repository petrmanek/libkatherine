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

/**
 * Bytes in one measurement datum on the wire, for a Timepix3 readout.
 *
 * Prefixed because it branches on the ASIC: a Timepix4 readout sends eight
 * bytes, where this family sends six. The value is the readout's, not the
 * chip's -- Timepix2 readouts send six as well -- and the prefix follows
 * the library's convention rather than naming a family it has no other
 * word for.
 */
#define KATHERINE_TPX3_MD_SIZE 6

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
 *
 * \note A handler runs on the thread inside katherine_acquisition_read(),
 *   not on one of its own. katherine_acquisition_abort() may be called from
 *   a handler and ends the read. katherine_acquisition_stop() may also be
 *   called, but it asks the readout to stop rather than the loop, so the
 *   read ends when the data does. katherine_acquisition_read() must not be
 *   re-entered: the inner call takes part of the stream and neither result
 *   means anything afterwards.
 */
typedef struct katherine_acquisition_handlers {
    // If katherine_acquisition_t::decode_data == true, you may register
    // handlers for the following callbacks:
    void (*pixels_received)(void *, const void *, size_t);                  ///< Decoded pixels, in batches
    void (*frame_started)(void *, int);                                     ///< New frame opened. The int is katherine_acquisition_t::completed_frames, a running count and not an identifier: a frame the readout abandons does not raise it, so the next frame is announced with the same number.
    void (*frame_ended)(void *, int, bool, const katherine_frame_info_t *); ///< Current frame closed, with its info. Raised for every frame frame_started() announced, the bool saying whether the readout finished it.

    // If katherine_acquisition_t::decode_data == false, you may register
    // handlers for the following callbacks:
    void (*data_received)(void *, const char *, size_t); ///< Raw, undecoded measurement data as received
} katherine_acquisition_handlers_t;

/** Bytes katherine_acquisition_t reserves for the decoder's private state. */
#define KATHERINE_ACQUISITION_PRIV_SIZE 384

/**
 * Opaque storage for an acquisition's decoder state.
 *
 * A caller has no business reading this and the library casts it to whatever
 * the ASIC in hand needs. The second member is there for its alignment, not
 * for its value.
 */
typedef union katherine_acquisition_priv {
    uint8_t storage[KATHERINE_ACQUISITION_PRIV_SIZE]; ///< The bytes themselves. Never read as bytes.
    uint64_t alignment;                               ///< Never read. Its presence is the union's alignment.
} katherine_acquisition_priv_t;

/** Where an acquisition has got to; read it from katherine_acquisition_t::state. */
typedef enum katherine_acquisition_state {
    KATHERINE_ACQUISITION_STATE_NOT_STARTED = 0, ///< The detector is not sensitive, call katherine_acquisition_begin to start measurement.
    KATHERINE_ACQUISITION_STATE_RUNNING     = 1, ///< The detector is sensitive and collecting data, call katherine_acquisition_read() to retrieve its output or katherine_acquisition_{stop,abort}() to interrupt it. Note that for decode_data == false, only katherine_acquisition_abort() is viable.
    KATHERINE_ACQUISITION_STATE_SUCCEEDED   = 2, ///< The detector is no longer sensitive, the last measurement data arrived and the measurement concluded in an orderly manner.
    KATHERINE_ACQUISITION_STATE_TIMED_OUT   = 3, ///< Every end of a read that is not an orderly one, the name notwithstanding: a communications timeout, where the detector may still be measuring and the data flow was disrupted or the device was powered off; and equally a link that closed or failed under the read. Which it was is in katherine_acquisition_read()'s return code, not here. A common end if decode_data == false and katherine_acquisition_abort() is not called when the measurement is over.
} katherine_acquisition_state_t;

/**
 * What became of katherine_tpx3_config_t::correct_clock_phase for a given
 * acquisition. Three outcomes, not two: a device that never staggers its
 * columns and one that corrects the stagger itself are different situations,
 * and only the second leaves an offset that has to be undone to recover the
 * sensor's own counters.
 */
typedef enum katherine_tpx3_phase_correction {
    KATHERINE_TPX3_PHASE_CORRECTION_NONE     = 0, ///< Not requested, or nothing to correct.
    KATHERINE_TPX3_PHASE_CORRECTION_SOFTWARE = 1, ///< Applied by the decoder.
    KATHERINE_TPX3_PHASE_CORRECTION_HARDWARE = 2, ///< Offloaded to the readout.
} katherine_tpx3_phase_correction_t;

KATHERINE_EXPORTED const char *
katherine_str_phase_correction(katherine_tpx3_phase_correction_t v);

/**
 * One acquisition: the device it runs on, what was asked of it, the buffers it
 * decodes through and the counters it fills in.
 *
 * Allocated by the caller and initialized by katherine_acquisition_init(), so
 * every member is visible. Only the handlers and the counters are a caller's
 * business; the rest is the read loop's state and is documented as such.
 */
typedef struct katherine_acquisition {
    katherine_device_t *device;           ///< Device this acquisition runs on, as passed to katherine_acquisition_init().
    katherine_transport_t data_transport; ///< Data channel, which receives collected measurements. Owned by the acquisition.
    void *user_ctx;                       ///< The caller's context, handed back to every handler untouched.

    katherine_acquisition_state_t state; ///< Where the acquisition has got to.
    bool aborted;                        ///< Internal: true once an abort has been requested, which is how the read loop tells a dried-up stream from a lost one. Read and written atomically by the library, katherine_acquisition_abort() being callable from another thread than the one reading.

    // PINNED for the ASIC work (Petr, 2026-09-04): nothing in an acquisition
    // should be chip-type aware, and these three are Timepix3's. They belong
    // in the device's own record of what it last configured -- written by
    // katherine_tpx3_configure(), read back by katherine_acquisition_begin().
    //
    // The configuration restructure was where that record was to appear and
    // it did not: WP21 landed with begin() taking a katherine_tpx3_config_t
    // and calling configure() itself, so these three are still read out of
    // the caller's configuration rather than out of the device. The fields
    // stay here until Timepix2 or Timepix4 forces the issue, and the record
    // is still unbuilt -- which is also why a configure() followed by a
    // begin() uploads every chip's pixel matrix twice.
    katherine_tpx3_readout_mode_t readout_mode; ///< Readout mode in force, chosen at katherine_acquisition_begin().
    katherine_tpx3_px_mode_t px_mode;           ///< Pixel mode in force, which decides the hit type the handlers receive.
    bool fast_vco_enabled;                      ///< Whether the fast oscillator is on, which decides the fast hit variant.

    /**
     * Capacity in bytes of the buffer measurement data is read into. Must
     * exceed the largest datagram a readout can send.
     *
     * The buffer itself belongs to the device's transport, which
     * katherine_acquisition_init() resizes to exactly this. Kept here
     * because it is what katherine_acquisition_t::truncated_measurement_data
     * is counted against.
     */
    size_t md_buffer_size;

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

    /**
     * Bytes of measurement data taken off the link since
     * katherine_acquisition_begin(), counted whether or not they were
     * decoded. The one quantity a decoding run and a raw run can be
     * compared on, which is what a loss measurement in data-driven mode
     * needs: there is no sent-count to compare against there, so the only
     * yardstick is the same acquisition's own byte volume.
     */
    uint64_t received_measurement_data;

    uint64_t acq_start_time;    ///< Internal: when the acquisition began, in nanoseconds on a monotonic clock whose zero is arbitrary. What katherine_acquisition_t::fail_timeout is counted from, and a difference of two such readings is the only thing it is good for.
    uint32_t report_timeout_ms; ///< Milliseconds of silence from the readout after which a partly filled pixel buffer is delivered; zero disables. Tested only when a receive has timed out, so the readout's silence is noticed in steps of the data link's own receive timeout and never sooner than one; katherine_transport_set_timeout() is what shortens that step.
    uint32_t fail_timeout_ms;   ///< Milliseconds allowed past the requested measurement's own duration before the acquisition is declared timed out; zero disables.

    katherine_acquisition_handlers_t handlers; ///< The callbacks; set them after katherine_acquisition_init() and before beginning.
    katherine_frame_info_t current_frame_info; ///< Internal: the frame being filled, handed to frame_ended when it closes.

    /** What the phase request resolved to, decided by katherine_acquisition_begin(). */
    katherine_tpx3_phase_correction_t phase_correction;

    /**
     * The decoder's own state, which belongs to the ASIC being read out and
     * is not part of this interface.
     *
     * Reserved inline rather than behind a pointer so that it costs no
     * allocation and no dereference, and sized for the largest any ASIC this
     * library drives needs. A later ASIC changes what is kept here and the
     * assertion that it fits, nothing else -- which is what lets 2.1 add one
     * without moving a public field or changing the SONAME.
     */
    katherine_acquisition_priv_t priv;

    bool frame_active; ///< Internal: whether a frame is currently open.
} katherine_acquisition_t;

KATHERINE_EXPORTED int
katherine_acquisition_snprint(char *buf, size_t cap, const katherine_acquisition_t *v);

KATHERINE_EXPORTED uint8_t
katherine_tpx3_timestamp_phase_offset(const katherine_acquisition_t *acq, katherine_tpx3_coord_t coord);

KATHERINE_EXPORTED katherine_error_t
katherine_acquisition_init(katherine_acquisition_t *acq, katherine_device_t *device, void *ctx, size_t md_buffer_size, size_t pixel_buffer_size, uint32_t report_timeout_ms, uint32_t fail_timeout_ms);

KATHERINE_EXPORTED void
katherine_acquisition_fini(katherine_acquisition_t *acq);

KATHERINE_EXPORTED katherine_error_t
katherine_acquisition_begin(katherine_acquisition_t *acq, const katherine_tpx3_config_t *config, bool decode_data);

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
