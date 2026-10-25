/**
 * \file
 * \brief Implementation of the data acquisition process.
 * \author Petr Mánek
 * \date 29.5.18
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <string.h>
#include <katherine/global.h>
#include <katherine/acquisition.h>
#include <katherine/error.h>
#include <katherine/toa.h>
#include "atomic.h"
#include "monoclock.h"
#include "protocol/cmd_interface.h"
#include "device/private.h"
#include "acq/private.h"
#include "protocol/md.h"
#include "transport/factory.h"

#ifndef DOXYGEN_SHOULD_SKIP_THIS

static inline void
flush_buffer(katherine_acquisition_t *acq)
{
    if (acq->handlers.pixels_received != NULL) {
        acq->handlers.pixels_received(acq->user_ctx, acq->pixel_buffer, acq->pixel_buffer_valid);
    }

    acq->current_frame_info.received_pixels += acq->pixel_buffer_valid;
    acq->pixel_buffer_valid = 0;
}

// The epoch bias, resolved once per acquisition rather than per use. Its size
// and the reasons for it live with katherine_tpx3_toa_epoch_bias(); this is
// only the spelling the cold paths below use.
#define KATHERINE_TOA_EPOCH_BIAS(priv) katherine_tpx3_toa_epoch_bias((priv)->toa_coarse_tick_to_fine_shift)

static inline void
handle_new_frame(katherine_acquisition_t *acq, const uint64_t *data)
{
    katherine_acquisition_tpx3_priv_t *priv = katherine_acquisition_tpx3_priv(acq);
    (void) data;

    memset(&acq->current_frame_info, 0, sizeof(katherine_frame_info_t));
    acq->current_frame_info.start_time_observed = time(NULL);
    acq->current_frame_info.completed           = false;
    acq->frame_active                           = true;

    // The coarse time of arrival restarts with the frame, so the offset the
    // previous frame delivered no longer applies: the hits arriving before
    // the first offset of this frame belong to its first window. Reset to the
    // epoch bias rather than to zero -- see katherine_acquisition_begin().
    priv->last_toa_offset = KATHERINE_TOA_EPOCH_BIAS(priv);

    if (acq->handlers.frame_started != NULL) {
        acq->handlers.frame_started(acq->user_ctx, acq->completed_frames);
    }
}


static inline void
handle_timestamp_offset_driven_mode(katherine_acquisition_t *acq, const uint64_t *data)
{
    katherine_acquisition_tpx3_priv_t *priv = katherine_acquisition_tpx3_priv(acq);

    // The datum counts wraps of the chip's 14-bit coarse counter; scaling by
    // the coarse tick puts the offset in the fine ticks the timestamp uses, so
    // the decoder adds it without scaling anything per hit. The product stays a
    // whole multiple of the coarse tick, which is what lets the chip's own
    // counters be recovered from a timestamp later.
    priv->last_toa_offset = KATHERINE_TOA_EPOCH_BIAS(priv)
        + ((uint64_t) EXTRACT(*data, md_time_offset, offset) << (14 + priv->toa_coarse_tick_to_fine_shift));
}

static inline void
handle_current_frame_finished(katherine_acquisition_t *acq, const uint64_t *data)
{
    acq->current_frame_info.end_time_observed = time(NULL);

    flush_buffer(acq);

    acq->current_frame_info.sent_pixels = EXTRACT(*data, md_frame_finished, n_sent);
    acq->current_frame_info.completed   = true;
    acq->frame_active                   = false;

    if (acq->handlers.frame_ended != NULL) {
        acq->handlers.frame_ended(acq->user_ctx, acq->completed_frames, true, &acq->current_frame_info);
    }

    ++acq->completed_frames;

    if (acq->completed_frames == acq->requested_frames) {
        acq->state = KATHERINE_ACQUISITION_STATE_SUCCEEDED;
    }
}

static inline void
handle_acquisition_interrupted(katherine_acquisition_t *acq)
{
    acq->current_frame_info.end_time_observed = time(NULL);

    flush_buffer(acq);

    acq->frame_active = false;

    // The frame-finished MD did not arrive, so sent_pixels is unknown and
    // current_frame_info.completed remains false.
    if (acq->handlers.frame_ended != NULL) {
        acq->handlers.frame_ended(acq->user_ctx, acq->completed_frames, false, &acq->current_frame_info);
    }
}

// The four timestamp words are a tail, and on the second generation the
// readout sends one tail per chip: ARM_APP/main.c loops
// `for (i = 0; i < chip_number; i++) sendFrameTail(i);` at both of its call
// sites, and sendFrameTail() ORs (chip_index & 0xF) << 32 into the 0x8 word.
//
// This library keeps neither. md_time_lsb is MASK(32), so the chip index is
// discarded, and the four handlers assign unconditionally, so on a two-chip
// readout katherine_frame_info_t::start_time and ::end_time end up holding
// whichever chip's tail arrived last. A caller referencing a chip-0 hit to
// the frame start is then referencing the wrong chip's shutter edge.
//
// Left as it is deliberately: which chip a single pair of fields should
// describe is an interface question, and the emulator emits one tail
// whatever chip_count says, so nothing in the suite can see the difference
// yet. Whether the two chips' timers agree at all is unmeasured.
static inline void
handle_frame_start_timestamp_lsb(katherine_acquisition_t *acq, const uint64_t *data)
{
    acq->current_frame_info.start_time.b.lsb = EXTRACT(*data, md_time_lsb, lsb);
}

static inline void
handle_frame_start_timestamp_msb(katherine_acquisition_t *acq, const uint64_t *data)
{
    acq->current_frame_info.start_time.b.msb = EXTRACT(*data, md_time_msb, msb);
}

static inline void
handle_frame_end_timestamp_lsb(katherine_acquisition_t *acq, const uint64_t *data)
{
    acq->current_frame_info.end_time.b.lsb = EXTRACT(*data, md_time_lsb, lsb);
}

static inline void
handle_frame_end_timestamp_msb(katherine_acquisition_t *acq, const uint64_t *data)
{
    acq->current_frame_info.end_time.b.msb = EXTRACT(*data, md_time_msb, msb);
}

static inline void
handle_lost_pixel_count(katherine_acquisition_t *acq, const uint64_t *data)
{
    acq->current_frame_info.lost_pixels += EXTRACT(*data, md_lost_px, n_lost);
}

static inline void
handle_aborted_measurement(katherine_acquisition_t *acq, const uint64_t *data)
{
    (void) data;

    katherine_atomic_store_bool(&acq->aborted, true);
}

static inline void
handle_trigger_info(katherine_acquisition_t *acq, const uint64_t *data)
{
    (void) acq;
    (void) data;
    // The info is discarded.
}

// Gen2 splits the trigger across two words, 0x4 carrying the timestamp's low
// half and 0x6 the high half with the channel mask and the event counter. Both
// are discarded, as the Gen1 trigger info above is and as the reference
// implementation also does -- what matters here is that they are recognized:
// 0x4 is the pixel header on Gen1, so a Gen2 stream decoded by the Gen1 loop
// turns every trigger into a fabricated hit with nonsense coordinates.
// Exposing the contents is an API question, the same one for both generations,
// and it is not answered by recognizing them.
static inline void
handle_trigger_event(katherine_acquisition_t *acq, const uint64_t *data)
{
    (void) acq;
    (void) data;
}

static inline void
handle_unknown_msg(katherine_acquisition_t *acq, const uint64_t *data)
{
    (void) data;

    ++acq->dropped_measurement_data;
}

#ifdef KATHERINE_DEBUG_ACQ
// clang-format off
static inline void
dump_config(const katherine_acquisition_t *acq, const katherine_tpx3_config_t *config)
{
    printf("---- Begin Acquisition Configuration ----\n");
    printf("Acquisition Mode:       %d\n",      acq->px_mode);
    printf("Read-out Mode:          %d\n",      acq->readout_mode);
    printf("Fast VCO:               %s\n",      acq->fast_vco_enabled ? "enabled" : "disabled");
    printf("\n");

    printf("Acquisition Time:       %0.3f s\n", config->common.acq_time / 1e9);
    printf("Frame Count:            %u\n",      config->common.frame_count);
    for (uint8_t i = 0; i < KATHERINE_CONFIG_MAX_BIAS_SUPPLIES; ++i) {
        printf("Bias %u:                 %0.3f V\n", (unsigned) i, (double) config->common.bias[i]);
    }
    printf("\n");

    printf("Start Trigger:          %s\n",      config->common.start_trigger.enabled ? "true" : "false");
    printf("Start Trigger Channel:  %d\n",      config->common.start_trigger.channel);
    printf("Start Trigger Signal:   %s edge\n", katherine_str_trigger_edge(config->common.start_trigger.edge));
    printf("Start Trigger Delay:    %s\n",      config->common.start_trigger.delayed ? "true" : "false");
    printf("Start Trigger Delay s:  %g\n",      config->common.start_trigger.delay_seconds);
    printf("Stop Trigger:           %s\n",      config->common.stop_trigger.enabled ? "true" : "false");
    printf("Stop Trigger Channel:   %d\n",      config->common.stop_trigger.channel);
    printf("Stop Trigger Signal:    %s edge\n", katherine_str_trigger_edge(config->common.stop_trigger.edge));
    printf("Stop Trigger Delay:     %s\n",      config->common.stop_trigger.delayed ? "true" : "false");
    printf("Stop Trigger Delay s:   %g\n",      config->common.stop_trigger.delay_seconds);
    printf("\n");

    printf("Phase:                  %d\n",      config->clock_phase);
    printf("Clock Frequency:        %d\n",      config->clock_frequency);
    printf("\n");

    for (uint8_t i = 0; i < config->common.chip_count; ++i) {
        const katherine_tpx3_chip_config_t *chip = &config->chips[i];

        printf("Chip %u:\n",                    (unsigned) i);
        printf("  - Gray Coding:      %s\n",    chip->gray_disable ? "disabled" : "enabled");
        printf("  - Polarity:         %s\n",    katherine_str_polarity(chip->polarity));
        printf("  - Pixels:           %d %d ... %d %d\n",
            chip->pixels.words[0],      chip->pixels.words[1],
            chip->pixels.words[16382],  chip->pixels.words[16383]);
        printf("  - DACs:\n");
        printf("      Ibias_Preamp_ON:    %d\n",       chip->dacs.named.Ibias_Preamp_ON);
        printf("      Ibias_Preamp_OFF:   %d\n",       chip->dacs.named.Ibias_Preamp_OFF);
        printf("      Vpreamp_NCAS:       %d\n",       chip->dacs.named.Vpreamp_NCAS);
        printf("      Ibias_Ikrum:        %d\n",       chip->dacs.named.Ibias_Ikrum);
        printf("      Vfbk:               %d\n",       chip->dacs.named.Vfbk);
        printf("      Vthreshold_fine:    %d\n",       chip->dacs.named.Vthreshold_fine);
        printf("      Vthreshold_coarse:  %d\n",       chip->dacs.named.Vthreshold_coarse);
        printf("      Ibias_DiscS1_ON:    %d\n",       chip->dacs.named.Ibias_DiscS1_ON);
        printf("      Ibias_DiscS1_OFF:   %d\n",       chip->dacs.named.Ibias_DiscS1_OFF);
        printf("      Ibias_DiscS2_ON:    %d\n",       chip->dacs.named.Ibias_DiscS2_ON);
        printf("      Ibias_DiscS2_OFF:   %d\n",       chip->dacs.named.Ibias_DiscS2_OFF);
        printf("      Ibias_PixelDAC:     %d\n",       chip->dacs.named.Ibias_PixelDAC);
        printf("      Ibias_TPbufferIn:   %d\n",       chip->dacs.named.Ibias_TPbufferIn);
        printf("      Ibias_TPbufferOut:  %d\n",       chip->dacs.named.Ibias_TPbufferOut);
        printf("      VTP_coarse:         %d\n",       chip->dacs.named.VTP_coarse);
        printf("      VTP_fine:           %d\n",       chip->dacs.named.VTP_fine);
        printf("      Ibias_CP_PLL:       %d\n",       chip->dacs.named.Ibias_CP_PLL);
        printf("      PLL_Vcntrl:         %d\n",       chip->dacs.named.PLL_Vcntrl);
        printf("\n");
    }

    printf("Test Pulses:            %s\n",      config->test_pulses.enabled ? "enabled" : "disabled");
    printf("  - Destination:        %s\n",      config->test_pulses.digital_only ? "digital" : "analog");
    printf("  - Source:             %s\n",      config->test_pulses.external ? "external" : "internal");
    printf("  - Count:              %d\n",      config->test_pulses.count);
    printf("  - Period:             %d cc\n",   config->test_pulses.period);
    printf("  - Phase:              %d\n",      config->test_pulses.phase);

    printf("----  End Acquisition Configuration  ----\n");
}
// clang-format on
#endif /* KATHERINE_DEBUG_ACQ */

#endif /* DOXYGEN_SHOULD_SKIP_THIS */

/**
 * Initialize acquisition.
 *
 * md_buffer_size must be at least 65536 bytes on real hardware: the readout
 * sends measurement data as one UDP datagram per SDRAM slot, up to about
 * 65 kB, and a smaller buffer risks silently truncating a datagram (see
 * katherine_acquisition_t::truncated_measurement_data).
 *
 * The figure is the whole buffer, whatever the link makes of it: a UDP
 * session reads into one buffer of that size, while a USB ring divides it
 * across the reads it keeps outstanding, each rounded down to a whole frame.
 *
 * \param acq Acquisition to initialize
 * \param device Katherine device
 * \param ctx User context (may be used to convey useful info)
 * \param md_buffer_size Size of the measurement data buffer in bytes, taken
 *   together rather than per read. It has to exceed the largest datagram the
 *   readout sends, which depends on the model; 65536 covers every model
 *   measured so far. Zero asks the link for its own size.
 * \param pixel_buffer_size Size of the pixel buffer in bytes
 * \param report_timeout Silence after which a partly filled pixel buffer is
 *   delivered (ms). Set zero to disable. Noticed in steps of the data link's
 *   receive timeout, which bounds it from below -- see
 *   katherine_acquisition_t::report_timeout.
 * \param fail_timeout Timeout for any device communication (ms). Set zero to disable.
 *
 * \retval KATHERINE_E_OK on success, with
 *   katherine_acquisition_t::md_buffer_size holding the size the link
 *   settled on rather than the one asked for.
 * \retval KATHERINE_E_NOMEM if the data link's buffer or the pixel buffer
 *   could not be allocated; see malloc(3). The device is not contacted
 *   either way. A USB ring is held by the kernel while its reads are
 *   outstanding, and the budget for that is usbfs's rather than the host's:
 *   /sys/module/usbcore/parameters/usbfs_memory_mb, 16 MiB by default. A
 *   ring asking for more is refused here however much memory the host has.
 *
 * The codes below belong to the data link this opens, not to the acquisition.
 * Nothing is sent to the readout here -- the link is opened at the size asked
 * for, and every one of these is a failure of that.
 *
 * \retval KATHERINE_E_ADDRESS if the device's address names no link this
 *   build can open, or names one malformed; see katherine_device_init().
 * \retval KATHERINE_E_INVAL if the device's address carries a scheme this
 *   build was compiled without, the data link rejected an argument of its own
 *   open, or \p md_buffer_size is a size this link cannot serve at all. A
 *   size it can approximate is approximated; one outside its range is an
 *   error, and this is where it is reported rather than mid-measurement.
 * \retval KATHERINE_E_UNSUPPORTED if the address names a transport this
 *   build does not carry at all; see KATHERINE_FEATURE_UDP and
 *   KATHERINE_FEATURE_USB.
 * \retval KATHERINE_E_IO if the data socket could not be created or
 *   configured; see socket(2), setsockopt(2) and
 *   katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_SYSTEM if opening the data link failed at the OS level
 *   for a reason none of the other codes cover -- binding it, resolving its
 *   address, or taking its lock; see bind(2), inet_pton(3),
 *   pthread_mutex_init(3) and katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_CLOSED if a USB readout went away while its data
 *   endpoint was being claimed.
 * \retval KATHERINE_E_TIMEOUT if claiming a USB readout's data endpoint did
 *   not complete in time.
 */
katherine_error_t
katherine_acquisition_init(katherine_acquisition_t *acq, katherine_device_t *device, void *ctx, size_t md_buffer_size, size_t pixel_buffer_size, int report_timeout, int fail_timeout)
{
    // Before the first thing that can fail, and before anything is read back.
    // Three things turn on it: the phase state has to be defined before
    // begin() runs, since the offset accessor is readable on an initialized
    // acquisition and a stray HARDWARE value there would shift by an
    // indeterminate amount; the handlers are optional, so a caller who
    // registers some must not leave the rest pointing at indeterminate
    // values; and katherine_acquisition_fini() frees pixel_buffer, which the
    // transport failure below returns before assigning -- finalising then
    // used to free whatever the caller's object happened to hold.
    memset(acq, 0, sizeof(*acq));

    katherine_error_t res;

    acq->device   = device;
    acq->user_ctx = ctx;
    acq->state    = KATHERINE_ACQUISITION_STATE_NOT_STARTED;
    katherine_atomic_store_bool(&acq->aborted, false);

    // Opened here rather than with the device: nothing reads from one that is
    // not acquiring. A link claimed once is driven through the control
    // transport's claim, which is why that one outlives this.
#ifdef KATHERINE_FEATURE_UDP
    const uint16_t data_port = katherine_device_state(device)->udp_data_port;

#else
    // No link with a port space was built in, so there is no number to name.
    const uint16_t data_port = 0;
#endif /* KATHERINE_FEATURE_UDP */

    // The buffer the loop decodes belongs to the transport, so the size is
    // told to its constructor rather than allocated here -- and it is a hint:
    // a link serves the nearest size it can, zero asks for its own, and one
    // it cannot serve at all is refused here rather than mid-measurement.
    // What it settled on comes back, because truncated_measurement_data
    // counts against the size in force rather than the size asked for.
    res = katherine_transport_init_for_data(
        device->address, &device->control_transport, data_port, md_buffer_size, &acq->md_buffer_size, &acq->data_transport);
    if (res) goto err_no_transport;

    acq->pixel_buffer_size  = pixel_buffer_size;
    acq->pixel_buffer       = (char *) malloc(acq->pixel_buffer_size);
    acq->pixel_buffer_valid = 0;
    if (acq->pixel_buffer == NULL) {
        res = KATHERINE_E_NOMEM;
        goto err_pixel_buffer;
    }

    acq->report_timeout = report_timeout;
    acq->fail_timeout   = fail_timeout;

    return KATHERINE_E_OK;

err_pixel_buffer:
    katherine_transport_fini(&acq->data_transport);
err_no_transport:
    return res;
}

/**
 * Finalize acquisition
 * \param acq Acquisition to finalize
 *
 * Runs before katherine_device_fini() on the device this was initialized
 * against. An acquisition points into that device's transports while it
 * lives, so the device cannot be finalized under it; katherine_device_fini()
 * asserts on a device still holding one, and leaks its link rather than
 * tearing it down.
 */
void
katherine_acquisition_fini(katherine_acquisition_t *acq)
{
    if (acq->device == NULL) return;

    // Prevent a dangling pointer to invalid memory -- but only this
    // acquisition's own registration. The device holds one, the
    // mid-acquisition guard reads it, and clearing it on behalf of an
    // acquisition that does not own it would open that guard in the middle of
    // another measurement.
    if (acq->device->acquisition == acq) acq->device->acquisition = NULL;
    acq->device = NULL;

    free(acq->pixel_buffer);
    acq->pixel_buffer = NULL;

    // Before the control transport it was opened against.
    if (acq->data_transport.ops != NULL) katherine_transport_fini(&acq->data_transport);
}

/**
 * Switch cases for the headers every readout generation spells alike: the
 * frame lifecycle and the data-driven timestamp offset. Shared so that a
 * generation's map below declares only what makes it different.
 * \param acq Acquisition the handlers act on
 * \param md Measurement-data word being dispatched
 */
#define KATHERINE_MD_HEADER_CASES_COMMON(acq, md) \
    case 0x5: handle_timestamp_offset_driven_mode(acq, md); break; \
    case 0x7: handle_new_frame(acq, md); break; \
    case 0x8: handle_frame_start_timestamp_lsb(acq, md); break; \
    case 0x9: handle_frame_start_timestamp_msb(acq, md); break; \
    case 0xA: handle_frame_end_timestamp_lsb(acq, md); break; \
    case 0xB: handle_frame_end_timestamp_msb(acq, md); break; \
    case 0xC: handle_current_frame_finished(acq, md); break; \
    case 0xD: handle_lost_pixel_count(acq, md); break; \
    case 0xE: handle_aborted_measurement(acq, md); break

/**
 * Gen1's non-pixel headers: the trigger info under 0x2 and 0x3, plus the
 * common cases.
 * \param acq Acquisition the handlers act on
 * \param md Measurement-data word being dispatched
 */
#define KATHERINE_MD_HEADER_CASES_GEN1(acq, md) \
    case 0x2: \
    case 0x3: \
        handle_trigger_info(acq, md); \
        break; \
        KATHERINE_MD_HEADER_CASES_COMMON(acq, md)

/**
 * Gen2's non-pixel headers: the trigger event's two halves under 0x4 and 0x6,
 * plus the common cases. 0x2 and 0x3 are absent because Gen2 spends them on
 * chips 2 and 3.
 * \param acq Acquisition the handlers act on
 * \param md Measurement-data word being dispatched
 */
#define KATHERINE_MD_HEADER_CASES_GEN2(acq, md) \
    case 0x4: \
    case 0x6: \
        handle_trigger_event(acq, md); \
        break; \
        KATHERINE_MD_HEADER_CASES_COMMON(acq, md)

/**
 * Define one monomorphized decode loop.
 *
 * Instantiated per generation as well as per mode and divider, so that the
 * header map is a constant the compiler can fold: the pixel test becomes a
 * comparison against a literal, the chip index either a zero or the header
 * itself, and every case the generation cannot see is dead code. A runtime
 * header-normalization table would cost a load before the branch and keep both
 * generations' handlers live in one loop.
 *
 * \param SUFFIX Pixel type the loop decodes into
 * \param TAG Suffix distinguishing this instance from the mode's others
 * \param MAP md.h field map the words are decoded through
 * \param GEN Readout generation whose header map applies, GEN1 or GEN2
 */
#define DEFINE_ACQ_IMPL(SUFFIX, TAG, MAP, GEN) \
    static inline void \
    handle_measurement_data_##SUFFIX##TAG##_##GEN(katherine_acquisition_t *acq, const uint64_t *md) \
    { \
        char hdr = EXTRACT(*md, md, header); \
\
        if (KATHERINE_MD_HEADER_IS_PIXEL_##GEN(hdr)) { \
            if (acq->pixel_buffer_valid == acq->pixel_buffer_max_valid) { \
                flush_buffer(acq); \
            } \
\
            katherine_tpx3_px_##SUFFIX##_t *px = (katherine_tpx3_px_##SUFFIX##_t *) acq->pixel_buffer + acq->pixel_buffer_valid; \
\
            MAP(px, md, acq, (uint8_t) KATHERINE_MD_HEADER_CHIP_##GEN(hdr)); \
\
            ++acq->pixel_buffer_valid; \
        } else { \
            switch (hdr) { \
                KATHERINE_MD_HEADER_CASES_##GEN(acq, md); \
            default: handle_unknown_msg(acq, md); break; \
            } \
        } \
    } \
\
    static katherine_error_t \
    acquisition_read_##SUFFIX##TAG##_##GEN(katherine_acquisition_t *acq) \
    { \
        static const int PIXEL_SIZE = sizeof(katherine_tpx3_px_##SUFFIX##_t); \
\
        /* The one lock in the library that is taken without draining first. \
           Every control exchange flushes inside its lock so a stale \
           response cannot be read as an acknowledgement; this is the data \
           link, where what is waiting is measurement data and discarding \
           it would lose hits rather than noise. */ \
        katherine_error_t lock_res = katherine_transport_mutex_lock(&acq->data_transport); \
        if (lock_res != KATHERINE_E_OK) return lock_res; \
\
        uint64_t last_data_received = katherine_clock_monotonic_ns(); \
        double duration; \
        double kill_off_time = acq->fail_timeout <= 0 ? -1 : acq->requested_frames * acq->requested_frame_duration + (double) acq->fail_timeout / 1000.0; \
        katherine_error_t res; \
        katherine_error_t link_failure = KATHERINE_E_OK; \
\
        size_t i; \
        size_t received; \
\
        acq->pixel_buffer_valid     = 0; \
        acq->pixel_buffer_max_valid = acq->pixel_buffer_size / PIXEL_SIZE; \
\
        while (acq->state == KATHERINE_ACQUISITION_STATE_RUNNING) { \
            void *lent = NULL; \
            res        = katherine_transport_acquire_data_buf(&acq->data_transport, &lent, &received); \
\
            if (res) { \
                /* A timeout is the stream being quiet, and the wait it cost \
                   is what paces this loop. Any other code is the link \
                   itself failing -- gone, or refusing to lend a buffer -- \
                   which no number of retries can mend, and which on USB \
                   returns at once, so retrying it is a spin. */ \
                if (res != KATHERINE_E_TIMEOUT) { \
                    link_failure = res; \
                    acq->state   = KATHERINE_ACQUISITION_STATE_TIMED_OUT; \
                    continue; \
                } \
\
                duration = (double) (katherine_clock_monotonic_ns() - last_data_received) / 1000000.0; \
                if (acq->report_timeout > 0 && duration > acq->report_timeout && acq->pixel_buffer_valid > 0) { \
                    flush_buffer(acq); \
                } \
\
                duration = (double) (katherine_clock_monotonic_ns() - acq->acq_start_time) / 1000000000.0; \
                if (kill_off_time > 0 && duration > kill_off_time) { \
                    acq->state = KATHERINE_ACQUISITION_STATE_TIMED_OUT; \
                } \
\
                /* An abort ends the acquisition as soon as the stream has \
                   dried up, whether or not the data are decoded. The flag \
                   is raised by katherine_acquisition_abort() and by the \
                   aborted measurement datum alike. */ \
                if (katherine_atomic_load_bool(&acq->aborted)) { \
                    acq->state = KATHERINE_ACQUISITION_STATE_SUCCEEDED; \
                } \
                continue; \
            } \
\
            last_data_received = katherine_clock_monotonic_ns(); \
\
            /* A datagram that exactly fills the buffer may be intact, or may \
               have been longer on the wire and cut off exactly at the \
               buffer boundary -- the two are indistinguishable from the \
               byte count alone. MSG_TRUNC would tell them apart exactly but \
               is platform-specific; this is the portable heuristic instead, \
               so it can overcount (a datagram that just happens to fill the \
               buffer) but never miss a real truncation. */ \
            if (received == acq->md_buffer_size) ++acq->truncated_measurement_data; \
\
            /* Counted before the branch, not inside it: data_received is an \
               else-if, so without this a caller could never see decoded hits \
               and byte volume in the same run. */ \
            acq->received_measurement_data += received; \
\
            if (acq->decode_data) { \
                const char *it = (const char *) lent; \
                /* Whole data only: the trailing fragment of a datagram cut \
                   short is not a datum, and decoding it would decode the \
                   bytes that happen to follow it in the buffer. */ \
                for (i = 0; i + KATHERINE_MD_SIZE <= received; i += KATHERINE_MD_SIZE, it += KATHERINE_MD_SIZE) { \
                    /* Aligned copy replacing a misaligned cast: identical code on \
                       x86-64, AArch64 and PPC64; on ARMv7 two loads, not a faulting LDRD. */ \
                    uint64_t word; \
                    memcpy(&word, it, sizeof(word)); \
                    handle_measurement_data_##SUFFIX##TAG##_##GEN(acq, &word); \
                } \
            } else if (acq->handlers.data_received != NULL) { \
                acq->handlers.data_received(acq->user_ctx, (const char *) lent, received); \
            } \
\
            (void) katherine_transport_release_data_buf(&acq->data_transport, lent); \
        } \
\
        if (acq->frame_active) { \
            handle_acquisition_interrupted(acq); \
        } else if (acq->pixel_buffer_valid > 0) { \
            flush_buffer(acq); \
        } \
\
        (void) katherine_transport_mutex_unlock(&acq->data_transport); \
        /* What the link failed with, where it failed: more precise than the \
           timeout the state alone can express, and the state is the one the \
           caller already handles. */ \
        if (link_failure != KATHERINE_E_OK) return link_failure; \
\
        switch (acq->state) { \
        case KATHERINE_ACQUISITION_STATE_SUCCEEDED: return KATHERINE_E_OK; \
        case KATHERINE_ACQUISITION_STATE_TIMED_OUT: return KATHERINE_E_TIMEOUT; \
        /* Reachable only if the read loop above never ran at all, i.e. \
           katherine_acquisition_read() was called on an acquisition that \
           katherine_acquisition_begin() never brought to KATHERINE_ACQUISITION_STATE_RUNNING: \
           the two other terminal states are the explicit cases above. */ \
        default: return KATHERINE_E_STATE; \
        } \
    }

/**
 * Define the decode loops of one timestamp-bearing mode, for one divider.
 *
 * Such modes are instantiated once per pixel-clock divider so the
 * coarse-to-fine scale is a constant in the decode loop; see md.h. The two
 * Event+iToT modes carry no timestamp and so need only one instance each.
 *
 * \param SUFFIX Pixel type the loop decodes into
 * \param SHIFT log2 of the fine ticks in one coarse tick
 */
#define DEFINE_ACQ_IMPL_SHIFTED(SUFFIX, SHIFT, GEN) \
    DEFINE_ACQ_IMPL(SUFFIX, _s##SHIFT, pmd_##SUFFIX##_s##SHIFT##_map, GEN)

/**
 * Define the decode loops of one timestamp-bearing mode, for every divider.
 * \param SUFFIX Pixel type the loops decode into
 */
#define DEFINE_ACQ_IMPL_EVERY_SHIFT(SUFFIX, GEN) \
    DEFINE_ACQ_IMPL_SHIFTED(SUFFIX, 2, GEN) \
    DEFINE_ACQ_IMPL_SHIFTED(SUFFIX, 3, GEN) \
    DEFINE_ACQ_IMPL_SHIFTED(SUFFIX, 4, GEN) \
    DEFINE_ACQ_IMPL_SHIFTED(SUFFIX, 5, GEN)

/**
 * Instantiate one mode's loops for both readout generations.
 *
 * Eighteen loops exist per generation -- four dividers for each of the four
 * timestamp-bearing modes, plus one each for the two Event+iToT modes -- so
 * thirty-six in all.
 *
 * \param M Instantiating macro to apply, shifted or unshifted
 * \param SUFFIX Pixel type the loops decode into
 */
#define DEFINE_ACQ_IMPL_EVERY_GEN(M, SUFFIX) \
    M(SUFFIX, GEN1) \
    M(SUFFIX, GEN2)

DEFINE_ACQ_IMPL_EVERY_GEN(DEFINE_ACQ_IMPL_EVERY_SHIFT, f_toa_tot)
DEFINE_ACQ_IMPL_EVERY_GEN(DEFINE_ACQ_IMPL_EVERY_SHIFT, toa_tot)
DEFINE_ACQ_IMPL_EVERY_GEN(DEFINE_ACQ_IMPL_EVERY_SHIFT, f_toa_only)
DEFINE_ACQ_IMPL_EVERY_GEN(DEFINE_ACQ_IMPL_EVERY_SHIFT, toa_only)

/**
 * Define the decode loops of one mode that carries no timestamp, and so needs
 * no per-divider instance.
 * \param SUFFIX Pixel type the loop decodes into
 * \param GEN Readout generation whose header map applies
 */
#define DEFINE_ACQ_IMPL_UNSHIFTED(SUFFIX, GEN) DEFINE_ACQ_IMPL(SUFFIX, , pmd_##SUFFIX##_map, GEN)

DEFINE_ACQ_IMPL_EVERY_GEN(DEFINE_ACQ_IMPL_UNSHIFTED, f_event_count_itot)
DEFINE_ACQ_IMPL_EVERY_GEN(DEFINE_ACQ_IMPL_UNSHIFTED, event_count_itot)

#undef DEFINE_ACQ_IMPL_UNSHIFTED
#undef DEFINE_ACQ_IMPL_EVERY_GEN
#undef DEFINE_ACQ_IMPL_EVERY_SHIFT
#undef DEFINE_ACQ_IMPL_SHIFTED
#undef DEFINE_ACQ_IMPL

/**
 * The pixel clock reaches double column i on phase (i mod n), each phase a
 * coarse tick divided by n later than the last, so a hit there is timed
 * against an edge that much later than one in the first column -- and the
 * timestamp comes out that much smaller. Adding the offset back is what
 * equalises hits that arrived together.
 *
 * Both halves of a double column share a phase, hence x / 2. In fine ticks
 * the step is a whole number for every frequency and phase count, because a
 * coarse tick is a power-of-two multiple of the fine tick and the clamped
 * phase count divides it -- but only the CLAMPED count, from
 * katherine_tpx3_actual_phases(), divides it. The enumerator's own name does not:
 * KATHERINE_TPX3_PHASE_16 at KATHERINE_TPX3_FREQ_160_MHZ would give a quarter
 * of a fine tick.
 *
 * Internal on purpose: the offsets reach callers through
 * katherine_acquisition_timestamp_phase_offset(), not as a table of their
 * own.
 *
 * \param coarse_tick_to_fine_shift Bitshift needed to calculate the number of
 *   fine clock ticks in a single coarse clock tick.
 * \param phase_count Actual number of phases.
 * \param x Pixel X-coordinate (0..255), adjacent pixels within a
 *   double-column receive the same return value.
 * \return Offset applied to this pixel's column, in fine-oscillator ticks.
 */
static uint8_t
katherine_tpx3_toa_phase_offset(uint8_t coarse_tick_to_fine_shift, uint8_t phase_count, uint8_t x)
{
    if (phase_count <= 1) return 0;

    const uint8_t fine_ticks = (uint8_t) (1u << coarse_tick_to_fine_shift);
    const uint8_t step       = (uint8_t) (fine_ticks / phase_count);

    return (uint8_t) (((x / 2u) % phase_count) * step);
}

/**
 * Which of the three outcomes a phase-correction request resolves to. Ordered
 * so that the cheap, certain answers come first: a caller who did not ask gets
 * NONE without the device being consulted, and a configuration with a single
 * phase gets NONE because there is genuinely nothing to correct -- which is
 * worth reporting rather than papering over with a table of zeroes. Only then
 * is the readout asked whether it will do the work itself.
 *
 * Internal on purpose.
 *
 * \param acq Acquisition, where phase-correction may be applied.
 * \param config Readout configuration, which determines phase-correction behavior.
 */
static katherine_tpx3_phase_correction_t
resolve_phase_correction(const katherine_acquisition_t *acq, const katherine_tpx3_config_t *config)
{
    const katherine_acquisition_tpx3_priv_t *priv = katherine_acquisition_tpx3_priv_const(acq);

    // Not applied if not requested, or all columns use the same phase.
    if (!config->correct_clock_phase) return KATHERINE_TPX3_PHASE_CORRECTION_NONE;
    if (priv->phase_count <= 1) return KATHERINE_TPX3_PHASE_CORRECTION_NONE;

    // Offloaded to hardware if supported.
    if (katherine_device_can_correct_timestamp_phase(acq->device))
        return KATHERINE_TPX3_PHASE_CORRECTION_HARDWARE;

    // Otherwise: there are at least two phases, correction was requested, and hardware does not support offloading
    //            --> we got to do it in software.
    return KATHERINE_TPX3_PHASE_CORRECTION_SOFTWARE;
}

/**
 * Phase offset applied to a pixel's double column, in fine-oscillator ticks.
 *
 * The pixel clock reaches the double columns in staggered phases, so hits in
 * different columns are timed against edges that do not coincide. Correcting
 * for that means knowing the offset applied to the column a hit came from --
 * and undoing it again is what lets
 * katherine_tpx3_timestamp_to_toa_ftoa() recover the chip's own counters,
 * since a phase offset is not a whole coarse tick and would otherwise corrupt
 * the residue the fine term is read from.
 *
 * This reports what was actually applied, so callers need not track whether
 * correction is in effect: with none, every column reports zero and the
 * arithmetic downstream is unchanged. Reading it rather than assuming zero is
 * what keeps such code correct if correction is later switched on.
 *
 * \param acq Acquisition the pixel was decoded by.
 * \param coord Pixel coordinates.
 * \return Offset applied to this pixel's column, in fine-oscillator ticks.
 */
uint8_t
katherine_acquisition_timestamp_phase_offset(const katherine_acquisition_t *acq, katherine_tpx3_coord_t coord)
{
    const katherine_acquisition_tpx3_priv_t *priv = katherine_acquisition_tpx3_priv_const(acq);

    // The table is filled only where the decoder does the work. Where the
    // readout did it instead, the offset is real but absent from the table, so
    // it is computed -- this is a cold path and the arithmetic is the same
    // closed form the table was built from.
    if (acq->phase_correction == KATHERINE_TPX3_PHASE_CORRECTION_HARDWARE) {
        return katherine_tpx3_toa_phase_offset(priv->toa_coarse_tick_to_fine_shift, priv->phase_count, coord.x);
    }

    return priv->phase_offsets[coord.x];
}


/**
 * Reach the decode loop for one generation, across every mode and divider.
 *
 * Three dimensions, not two: the pixel format, the pixel-clock divider the
 * timestamp decoders are instantiated over, and the readout generation whose
 * header map applies. A divider outside the set means the acquisition never
 * went through katherine_acquisition_begin(), and is refused rather than
 * guessed -- guessing would misscale every timestamp in the run.
 *
 * Written once and instantiated per generation, so that the thirty-six loops
 * are reached without thirty-six lines of near-identical dispatch. The
 * generation is decided once per call, never inside the loop.
 *
 * \param acq Acquisition to read
 * \param priv Its decoder state, which the pixel-clock divider is read from
 * \param res Lvalue the loop's result is assigned to
 * \param GEN Readout generation whose loops to dispatch to
 */
#define ACQ_DISPATCH(acq, priv, res, GEN) \
    switch ((acq)->px_mode) { \
    case KATHERINE_TPX3_PX_TOA_TOT: \
        if ((acq)->fast_vco_enabled) { \
            switch ((priv)->toa_coarse_tick_to_fine_shift) { \
            case 2:  (res) = acquisition_read_f_toa_tot_s2##_##GEN(acq); break; \
            case 3:  (res) = acquisition_read_f_toa_tot_s3##_##GEN(acq); break; \
            case 4:  (res) = acquisition_read_f_toa_tot_s4##_##GEN(acq); break; \
            case 5:  (res) = acquisition_read_f_toa_tot_s5##_##GEN(acq); break; \
            default: (res) = KATHERINE_E_INVAL; break; \
            } \
        } else { \
            switch ((priv)->toa_coarse_tick_to_fine_shift) { \
            case 2:  (res) = acquisition_read_toa_tot_s2##_##GEN(acq); break; \
            case 3:  (res) = acquisition_read_toa_tot_s3##_##GEN(acq); break; \
            case 4:  (res) = acquisition_read_toa_tot_s4##_##GEN(acq); break; \
            case 5:  (res) = acquisition_read_toa_tot_s5##_##GEN(acq); break; \
            default: (res) = KATHERINE_E_INVAL; break; \
            } \
        } \
        break; \
    case KATHERINE_TPX3_PX_ONLY_TOA: \
        if ((acq)->fast_vco_enabled) { \
            switch ((priv)->toa_coarse_tick_to_fine_shift) { \
            case 2:  (res) = acquisition_read_f_toa_only_s2##_##GEN(acq); break; \
            case 3:  (res) = acquisition_read_f_toa_only_s3##_##GEN(acq); break; \
            case 4:  (res) = acquisition_read_f_toa_only_s4##_##GEN(acq); break; \
            case 5:  (res) = acquisition_read_f_toa_only_s5##_##GEN(acq); break; \
            default: (res) = KATHERINE_E_INVAL; break; \
            } \
        } else { \
            switch ((priv)->toa_coarse_tick_to_fine_shift) { \
            case 2:  (res) = acquisition_read_toa_only_s2##_##GEN(acq); break; \
            case 3:  (res) = acquisition_read_toa_only_s3##_##GEN(acq); break; \
            case 4:  (res) = acquisition_read_toa_only_s4##_##GEN(acq); break; \
            case 5:  (res) = acquisition_read_toa_only_s5##_##GEN(acq); break; \
            default: (res) = KATHERINE_E_INVAL; break; \
            } \
        } \
        break; \
    case KATHERINE_TPX3_PX_EVENT_COUNT_ITOT: \
        if ((acq)->fast_vco_enabled) { \
            (res) = acquisition_read_f_event_count_itot##_##GEN(acq); \
        } else { \
            (res) = acquisition_read_event_count_itot##_##GEN(acq); \
        } \
        break; \
    default: \
        (res) = KATHERINE_E_INVAL; \
        break; \
    }

/**
 * Read measurement data from acquisition.
 *
 * Returns once the acquisition leaves the running state, whether it finished,
 * timed out or was aborted. The device stops reporting a measurement in
 * flight at that point, so inquiries that a running acquisition refuses --
 * the sensor temperature among them -- become available again.
 *
 * What ends it differs by path. Decoding, it is the frame-finished datum for
 * the last requested frame. Not decoding, that datum is never examined, so
 * only an abort or this call's own timeout ends it. It is worth noting that
 * any such timeout may be either genuine, leaving the readout measuring, or
 * simply a consequence of receiving no further data past the frame-finished
 * datum (which we do not know arrived because we are not decoding).
 *
 * \param acq Acquisition
 *
 * \retval KATHERINE_E_OK on success, the acquisition having ended in an
 *   orderly manner: the last requested frame finished, or an abort ended it
 *   once the stream dried up. Everything decoded has reached the handlers.
 * \retval KATHERINE_E_TIMEOUT if the data stream fell silent and the whole
 *   requested measurement plus fail_timeout has elapsed since
 *   katherine_acquisition_begin(). The readout has gone quiet, which is not to
 *   say it stopped measuring; whatever was decoded up to that point has
 *   reached the handlers, and a frame still open was reported incomplete.
 * \retval KATHERINE_E_STATE if the acquisition was not running when the call
 *   was made, so the read loop never ran and nothing was read -- an
 *   acquisition katherine_acquisition_begin() never brought to
 *   KATHERINE_ACQUISITION_STATE_RUNNING -- or if the device has been neither
 *   enumerated nor declared, or is one this version cannot drive, since the
 *   measurement-data header map depends on a readout generation that is then
 *   not known. Reading an acquisition that already finished is neither case:
 *   it reports the outcome it finished with again.
 * \retval KATHERINE_E_INVAL if no decoder is instantiated for this
 *   acquisition's mode and pixel-clock divider -- an unknown px_mode, or a
 *   coarse-to-fine shift outside 2..5 in a timestamp-bearing mode -- rather
 *   than decoding every timestamp at a guessed scale.
 *
 * \note Which of those two a never-begun read reports depends on the pixel
 *   mode, the decoder being selected before the state is examined. The shift
 *   is resolved in katherine_acquisition_begin() and is zero until it runs, so
 *   a timestamp-bearing mode finds no decoder and reports
 *   KATHERINE_E_INVAL, while KATHERINE_TPX3_PX_EVENT_COUNT_ITOT needs no
 *   shift, finds its decoder and reaches the state check to report
 *   KATHERINE_E_STATE. Neither says more than that begin() has not run.
 * \retval KATHERINE_E_SYSTEM if the data link's lock, which the read holds for
 *   its whole duration, could not be taken; see
 *   katherine_transport_get_last_os_error(). That same lock failing with an errno
 *   the transport maps specially is reported as KATHERINE_E_NOMEM,
 *   KATHERINE_E_INVAL or KATHERINE_E_TIMEOUT instead.
 * \retval KATHERINE_E_NOMEM if taking the data link's lock ran out of memory;
 *   see katherine_transport_get_last_os_error().
 *
 * \note The codes below are returned from the per-generation dispatch macros,
 *   which the error indexer cannot follow, so it reports them as unreachable:
 *   KATHERINE_E_INVAL, KATHERINE_E_NOMEM, KATHERINE_E_SYSTEM and
 *   KATHERINE_E_TIMEOUT.
 */
katherine_error_t
katherine_acquisition_read(katherine_acquisition_t *acq)
{
    const katherine_acquisition_tpx3_priv_t *priv = katherine_acquisition_tpx3_priv_const(acq);
    katherine_error_t res;

    // The dispatch depends on generation, i.e. we need a successfully enumerated device.
    if (!acq->device->derived_info.supported) {
        res = KATHERINE_E_STATE;
        goto err_unsupported;
    }

    if (katherine_device_gen(acq->device) >= 2) {
        ACQ_DISPATCH(acq, priv, res, GEN2);
    } else {
        ACQ_DISPATCH(acq, priv, res, GEN1);
    }

#undef ACQ_DISPATCH

err_unsupported:
    if (acq->device->acquisition == acq) acq->device->acquisition = NULL;
    return res;
}

/**
 * Set detector configuration and begin acquisition.
 *
 * With decode_data false the caller takes on the obligation of ending the
 * acquisition, because nothing in the read loop will: see the field's
 * description in acquisition.h. katherine_acquisition_abort() is how.
 *
 * fast_vco_enable is refused with KATHERINE_E_INVAL at any clock_frequency
 * where fine time stamping is not coherent, since the resulting stream would
 * carry a fine field that cannot be subtracted from its coarse one.
 * katherine_tpx3_freq_is_fast_vco_supported reports which frequencies qualify, and
 * answers without starting an acquisition. Callers deriving times from the
 * phase count want katherine_tpx3_actual_phases for a related reason -- the phase
 * enumerators name what is asked for, not what the divider grants.
 *
 * Both refusals here, this one and the data-driven frame count, precede every
 * socket, so a failed begin() has sent nothing and left the device idle.
 *
 * \see katherine_tpx3_freq_is_fast_vco_supported
 * \see katherine_tpx3_actual_phases
 *
 * \param acq Acquisition
 * \param config Configuration, which carries the readout mode, the pixel
 *   mode and the oscillator setting that used to be arguments here
 * \param decode_data True to decode measurement data into pixels and track
 *   the frame lifecycle, false to hand the raw stream to the data_received
 *   handler; see katherine_acquisition_t::decode_data, which this sets
 *
 * \retval KATHERINE_E_OK on success, with the readout armed and the
 *   acquisition running.
 * \retval KATHERINE_E_INVAL if the combination asked for is refused: a data-
 *   driven readout of more than one frame, fast VCO at a frequency where fine
 *   time stamping is not coherent, or a configuration value out of range, the
 *   test-pulse count, period and phase among them. Also reported if one of the
 *   configuration commands failed with an invalid argument at the OS level;
 *   see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_TIMEOUT if the readout did not acknowledge one of the
 *   configuration commands within the control link's receive timeout. Sending
 *   stops at the first such failure, so the device is left partly configured
 *   and not measuring.
 * \retval KATHERINE_E_CLOSED if the link went away mid-exchange, which a
 *   removable one can do and a socket cannot: a USB readout unplugged or
 *   reset out from under the session; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_BAD_CRD if a configuration command was answered by a
 *   response that was not exactly the fixed size the protocol defines.
 * \retval KATHERINE_E_STRAY if non-correlating responses kept arriving on the
 *   control link until the discard budget ran out before the acknowledgement
 *   of a configuration command did.
 * \retval KATHERINE_E_IO if any of the control-plane sends or receives failed
 *   at the OS level for a reason none of the other codes cover; see
 *   katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_NOMEM if taking the control link's lock, or any of the
 *   control-plane sends and receives, ran out of memory; see
 *   katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_SYSTEM if the control link's lock could not be taken;
 *   see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_UNSUPPORTED if the end trigger's delay is armed on a
 *   readout of the first generation; see katherine_set_acquisition_triggers().
 * \retval KATHERINE_E_STATE if the device has been neither enumerated nor
 *   declared, or is one this version cannot drive. Refused here rather than at
 *   the first katherine_acquisition_read(), so a caller learns before the
 *   detector is made sensitive; katherine_configure() would refuse it anyway,
 *   on the acquisition time.
 * \retval KATHERINE_E_BAD_CHIP if the readout reports no chips at all, chip 0
 *   included. katherine_configure() addresses chip 0 at every per-chip step,
 *   so a device enumerated as carrying nothing fails there; see
 *   katherine_device_info_t::chip_count.
 */
katherine_error_t
katherine_acquisition_begin(katherine_acquisition_t *acq, const katherine_tpx3_config_t *config, bool decode_data)
{
    katherine_acquisition_tpx3_priv_t *priv = katherine_acquisition_tpx3_priv(acq);
    katherine_error_t res                   = 0;

    acq->px_mode          = config->px_mode;
    acq->readout_mode     = config->readout_mode;
    acq->fast_vco_enabled = config->fast_vco_enable;
    acq->decode_data      = decode_data;

#if KATHERINE_DEBUG_ACQ > 0
    dump_config(acq, config);
#endif /* KATHERINE_DEBUG_ACQ */

    if (config->readout_mode == KATHERINE_TPX3_READOUT_DATA_DRIVEN && config->common.frame_count > 1) {
        res = KATHERINE_E_INVAL;
        goto err;
    }

    // Fine time stamping is not coherent at every frequency; the predicate
    // owns the criterion. Refused here rather than passed to the sensor,
    // which would answer with a stream whose fine field cannot be subtracted
    // from its coarse one.
    if (config->fast_vco_enable && !katherine_tpx3_freq_is_fast_vco_supported(config->clock_frequency)) {
        res = KATHERINE_E_INVAL;
        goto err;
    }

    res = katherine_tpx3_configure(acq->device, config);
    if (res) goto err;

    res = katherine_tpx3_set_seq_readout_start(acq->device, config->readout_mode);
    if (res) goto err;

    res = katherine_tpx3_set_acq_mode(acq->device, config->px_mode, config->fast_vco_enable);
    if (res) goto err;

    acq->completed_frames           = 0;
    acq->requested_frames           = (int) config->common.frame_count;
    acq->requested_frame_duration   = config->common.acq_time / 1e9;
    acq->dropped_measurement_data   = 0;
    acq->truncated_measurement_data = 0;
    acq->received_measurement_data  = 0;

    acq->pixel_buffer_valid     = 0;
    acq->pixel_buffer_max_valid = 0;

    priv->toa_coarse_tick_to_fine_shift = katherine_tpx3_toa_coarse_tick_to_fine_shift(config->clock_frequency);
    priv->phase_count                   = katherine_tpx3_actual_phases(config->clock_frequency, config->clock_phase);
    acq->phase_correction               = resolve_phase_correction(acq, config);

    // Only SOFTWARE puts anything in the table. Every other outcome leaves it
    // zeroed, which is what lets the decoder add unconditionally.
    memset(priv->phase_offsets, 0, sizeof(priv->phase_offsets));
    if (acq->phase_correction == KATHERINE_TPX3_PHASE_CORRECTION_SOFTWARE) {
        // uint16_t, not uint8_t: the width is 256, so an eight-bit counter
        // is always below it and this loop would never end.
        for (uint16_t x = 0; x < KATHERINE_TPX3_MATRIX_WIDTH; ++x) {
            priv->phase_offsets[x] = katherine_tpx3_toa_phase_offset(priv->toa_coarse_tick_to_fine_shift, priv->phase_count, (uint8_t) x);
        }
    }
    priv->last_toa_offset = KATHERINE_TOA_EPOCH_BIAS(priv);
    acq->frame_active     = false;

    res = katherine_device_control_lock(acq->device);
    if (res) goto err;

    katherine_transport_flush_incoming(&acq->device->control_transport);

    acq->acq_start_time = katherine_clock_monotonic_ns();

    res = katherine_cmd_start_acquisition(&acq->device->control_transport, acq->readout_mode);
    if (res) goto err_cmd;

    (void) katherine_transport_mutex_unlock(&acq->device->control_transport);

    // Last, and after the readout has been told to start. Set before the
    // lock above, as it used to be, a failed lock or a failed start command
    // returned an acquisition that called itself RUNNING while the readout
    // was never started: katherine_acquisition_read() would then wait out
    // fail_timeout for data nobody asked for.
    acq->state = KATHERINE_ACQUISITION_STATE_RUNNING;

    // This creates user obligation to either stop or read the data.
    acq->device->acquisition = acq;
    return KATHERINE_E_OK;

err_cmd:
    (void) katherine_transport_mutex_unlock(&acq->device->control_transport);
err:
    return res;
}

/**
 * Stop acquisition. No acknowledgement exists for this command; the
 * readout signals the end of the current frame through the measurement
 * data stream instead.
 *
 * Sends the same command as katherine_acquisition_abort(), so the two are one
 * command as far as the readout is concerned; what differs is local, and is
 * described there.
 *
 * \see katherine_acquisition_abort
 *
 * \param acq Acquisition
 *
 * \retval KATHERINE_E_OK on success, meaning the stop command left the host.
 *   Nothing is waited for, so this is not a report that the readout received
 *   it or that the measurement has ended.
 * \retval KATHERINE_E_IO if sending the stop command failed at the OS level
 *   for a reason none of the other codes cover; see
 *   katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_INVAL if taking the control link's lock, or sending the
 *   stop command, reported an invalid argument; see
 *   katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_NOMEM if taking the control link's lock or sending the
 *   stop command ran out of memory; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_TIMEOUT if the control link's lock could not be taken
 *   for lack of a non-memory system resource -- never because the readout
 *   stayed silent, since this command carries no acknowledgement to wait for;
 *   see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_CLOSED if the link went away mid-exchange, which a
 *   removable one can do and a socket cannot: a USB readout unplugged or
 *   reset out from under the session; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_STATE if the acquisition's device has no control link,
 *   which is one katherine_device_init() was never called on or whose call
 *   failed.
 * \retval KATHERINE_E_SYSTEM if the control link's lock could not be taken,
 *   for a reason none of the other codes cover; see
 *   katherine_transport_get_last_os_error().
 */
katherine_error_t
katherine_acquisition_stop(katherine_acquisition_t *acq)
{
    katherine_error_t res;

    res = katherine_device_control_lock(acq->device);
    if (res) goto err_lock;

    katherine_transport_flush_incoming(&acq->device->control_transport);

    res = katherine_cmd_stop_acquisition(&acq->device->control_transport, acq->readout_mode);
    if (res) goto err_send;

    (void) katherine_transport_mutex_unlock(&acq->device->control_transport);
    return KATHERINE_E_OK;

err_send:
    (void) katherine_transport_mutex_unlock(&acq->device->control_transport);
err_lock:
    return res;
}


/**
 * Abort acquisition. This command does not wait for confirmation from the
 * readout and will cause the current frame to end upon receiving.
 *
 * For acquisitions with decode_data == false, this is the only way to achieve
 * orderly termination: it raises the flag that katherine_acquisition_read()
 * acts on once the stream dries up, which is the only thing that will end the
 * read short of its timeout.
 *
 * Note the flag it raises is shared with the readout's own
 * aborted-measurement datum, so an acquisition the hardware abandoned and one
 * the caller stopped both finish as KATHERINE_ACQUISITION_STATE_SUCCEEDED and
 * cannot be told apart afterwards.
 *
 * \param acq Acquisition
 *
 * \retval KATHERINE_E_OK on success, meaning the stop command left the host
 *   and the flag katherine_acquisition_read() ends on has been raised. The
 *   flag is raised on this path only, so a call that failed leaves the read
 *   with nothing to end it and must be repeated.
 * \retval KATHERINE_E_IO if sending the stop command failed at the OS level
 *   for a reason none of the other codes cover; see
 *   katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_INVAL if taking the control link's lock, or sending the
 *   stop command, reported an invalid argument; see
 *   katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_NOMEM if taking the control link's lock or sending the
 *   stop command ran out of memory; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_TIMEOUT if the control link's lock could not be taken
 *   for lack of a non-memory system resource -- never because the readout
 *   stayed silent, since this command carries no acknowledgement to wait for;
 *   see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_CLOSED if the link went away mid-exchange, which a
 *   removable one can do and a socket cannot: a USB readout unplugged or
 *   reset out from under the session; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_STATE if the acquisition's device has no control link,
 *   which is one katherine_device_init() was never called on or whose call
 *   failed.
 * \retval KATHERINE_E_SYSTEM if the control link's lock could not be taken,
 *   for a reason none of the other codes cover; see
 *   katherine_transport_get_last_os_error().
 *
 * Sends the same command as katherine_acquisition_stop() -- opcode 0x06 with
 * the read mode -- so neither is distinguishable from the other on the wire.
 * What this one adds is local: it raises katherine_acquisition_t::aborted, and
 * that flag is tested inside the receive-timeout branch of the read loop
 * rather than on its own. The flag goes up whether or not the command
 * reaches the readout, so a returned error says the readout was not told,
 * never that the run was left running. So it does not end a run by itself; it changes what
 * a stream running dry is taken to mean.
 *
 * On Gen2 those compose into a working abort. Measured on hw_type 3 /
 * fw_version 5: the command closes the shutter, a data-driven stream ceases
 * about 9 ms later against an acquisition time of 5 s that an unsignalled run
 * of the same configuration uses in full, and the flag turns the ensuing
 * timeout into KATHERINE_ACQUISITION_STATE_SUCCEEDED.
 *
 * Whether the readout acts on the command at all is firmware-dependent and
 * unverified outside that one Gen2 build, so a caller cannot read
 * KATHERINE_E_OK as meaning the measurement has ended -- only that the
 * command left the host.
 */
katherine_error_t
katherine_acquisition_abort(katherine_acquisition_t *acq)
{
    katherine_error_t res;

    // Before the command, not after it succeeds: the flag is this call's local
    // half, and a readout that can no longer be told to stop is exactly when
    // the read loop most needs to know the run is over. Raising it only on a
    // command that landed left an unreachable readout unabortable.
    katherine_atomic_store_bool(&acq->aborted, true);

    if ((res = katherine_device_control_lock(acq->device)) != KATHERINE_E_OK) goto err_lock;

    katherine_transport_flush_incoming(&acq->device->control_transport);

    res = katherine_cmd_stop_acquisition(&acq->device->control_transport, acq->readout_mode);

    (void) katherine_transport_mutex_unlock(&acq->device->control_transport);
    if (res) goto err_stop;

    return KATHERINE_E_OK;

err_stop:
err_lock:
    return res;
}

/**
 * Get human-readable description of acquisition status.
 *
 * The tokens are the same style katherine_str_readout_mode(),
 * katherine_str_px_mode(), katherine_str_phase() and
 * katherine_str_freq() use (repr.c): lowercase, underscore-separated,
 * stable across releases. Note for callers of the 1.x series: this
 * function used to return "not started" and "timed out" (with a space);
 * it now returns "not_started" and "timed_out" to match.
 *
 * \param state State to describe
 * \return Null-terminated string.
 */
const char *
katherine_str_acquisition_state(katherine_acquisition_state_t state)
{
    switch (state) {
    case KATHERINE_ACQUISITION_STATE_NOT_STARTED: return "not_started";
    case KATHERINE_ACQUISITION_STATE_SUCCEEDED:   return "succeeded";
    case KATHERINE_ACQUISITION_STATE_RUNNING:     return "running";
    case KATHERINE_ACQUISITION_STATE_TIMED_OUT:   return "timed_out";
    default:                                      return "unknown";
    }
}
