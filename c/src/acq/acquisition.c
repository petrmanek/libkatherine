/**
 * @file
 * @brief Implementation of the data acquisition process.
 * @author Petr Mánek
 * @date 29.5.18
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
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
#include "protocol/cmd_interface.h"
#include "protocol/md.h"

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

/* The epoch bias, resolved once per acquisition rather than per use. Its size
   and the reasons for it live with katherine_tpx3_toa_epoch_bias(); this is
   only the spelling the cold paths below use. */
#define KATHERINE_TOA_EPOCH_BIAS(acq) \
    katherine_tpx3_toa_epoch_bias((acq)->toa_coarse_tick_to_fine_shift)

static inline void
handle_new_frame(katherine_acquisition_t *acq, const uint64_t *data)
{
    (void) data;

    memset(&acq->current_frame_info, 0, sizeof(katherine_frame_info_t));
    acq->current_frame_info.start_time_observed = time(NULL);
    acq->current_frame_info.completed           = false;
    acq->frame_active                           = true;

    // The coarse time of arrival restarts with the frame, so the offset the
    // previous frame delivered no longer applies: the hits arriving before
    // the first offset of this frame belong to its first window. Reset to the
    // epoch bias rather than to zero -- see katherine_acquisition_begin().
    acq->last_toa_offset = KATHERINE_TOA_EPOCH_BIAS(acq);

    if (acq->handlers.frame_started != NULL) {
        acq->handlers.frame_started(acq->user_ctx, acq->completed_frames);
    }
}


static inline void
handle_timestamp_offset_driven_mode(katherine_acquisition_t *acq, const uint64_t *data)
{
    /* The datum counts wraps of the chip's 14-bit coarse counter; scaling by
       the coarse tick puts the offset in the fine ticks the timestamp uses, so
       the decoder adds it without scaling anything per hit. The product stays a
       whole multiple of the coarse tick, which is what lets the chip's own
       counters be recovered from a timestamp later. */
    acq->last_toa_offset = KATHERINE_TOA_EPOCH_BIAS(acq)
        + ((uint64_t) EXTRACT(*data, md_time_offset, offset) << (14 + acq->toa_coarse_tick_to_fine_shift));
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
        acq->state = ACQUISITION_SUCCEEDED;
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

    acq->aborted = true;
}

static inline void
handle_trigger_info(katherine_acquisition_t *acq, const uint64_t *data)
{
    (void) acq;
    (void) data;
    /* The info is discarded. */
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
dump_config(const katherine_acquisition_t *acq, const katherine_config_t *config)
{
    printf("---- Begin Acquisition Configuration ----\n");
    printf("Acquisition Mode:       %d\n",      acq->acq_mode);
    printf("Read-out Mode:          %d\n",      acq->readout_mode);
    printf("Fast VCO:               %s\n",      acq->fast_vco_enabled ? "enabled" : "disabled");
    printf("\n");

    printf("Bias ID:                %d\n",      config->bias_id);
    printf("Acquisition Time:       %0.3f s\n", config->acq_time / 1e9);
    printf("No. of Frames:          %d\n",      config->no_frames);
    printf("Bias:                   %0.3f V\n", config->bias);
    printf("\n");

    printf("Delay Start:            %s\n",      config->delayed_start ? "true" : "false");
    printf("Start Trigger:          %s\n",      config->start_trigger.enabled ? "true" : "false");
    printf("Start Trigger Channel:  %d\n",      config->start_trigger.channel);
    printf("Start Trigger Signal:   %s\n",      config->start_trigger.use_falling_edge ? "falling edge" : "rising edge");
    printf("Stop Trigger:           %s\n",      config->stop_trigger.enabled ? "true" : "false");
    printf("Stop Trigger Channel:   %d\n",      config->stop_trigger.channel);
    printf("Stop Trigger Signal:    %s\n",      config->stop_trigger.use_falling_edge ? "falling edge" : "rising edge");
    printf("\n");

    printf("Gray Coding:            %s\n",      config->gray_disable ? "disabled" : "enabled");
    printf("Polarity:               %s\n",      config->polarity_holes ? "holes (h+)" : "electrons (e-)");
    printf("Phase:                  %d\n",      config->phase);
    printf("Clock Frequency:        %d\n",      config->freq);
    printf("\n");

    printf("Pixel Configuration:    %d %d ... %d %d\n",
        config->pixel_config.words[0],      config->pixel_config.words[1],
        config->pixel_config.words[16382],  config->pixel_config.words[16383]);
    printf("\n");

    printf("DACs:\n");
    printf("  - Ibias_Preamp_ON:    %d\n",      config->dacs.named.Ibias_Preamp_ON);
    printf("  - Ibias_Preamp_OFF:   %d\n",      config->dacs.named.Ibias_Preamp_OFF);
    printf("  - VPReamp_NCAS:       %d\n",      config->dacs.named.VPReamp_NCAS);
    printf("  - Ibias_Ikrum:        %d\n",      config->dacs.named.Ibias_Ikrum);
    printf("  - Vfbk:               %d\n",      config->dacs.named.Vfbk);
    printf("  - Vthreshold_fine:    %d\n",      config->dacs.named.Vthreshold_fine);
    printf("  - Vthreshold_coarse:  %d\n",      config->dacs.named.Vthreshold_coarse);
    printf("  - Ibias_DiscS1_ON:    %d\n",      config->dacs.named.Ibias_DiscS1_ON);
    printf("  - Ibias_DiscS1_OFF:   %d\n",      config->dacs.named.Ibias_DiscS1_OFF);
    printf("  - Ibias_DiscS2_ON:    %d\n",      config->dacs.named.Ibias_DiscS2_ON);
    printf("  - Ibias_DiscS2_OFF:   %d\n",      config->dacs.named.Ibias_DiscS2_OFF);
    printf("  - Ibias_PixelDAC:     %d\n",      config->dacs.named.Ibias_PixelDAC);
    printf("  - Ibias_TPbufferIn:   %d\n",      config->dacs.named.Ibias_TPbufferIn);
    printf("  - Ibias_TPbufferOut:  %d\n",      config->dacs.named.Ibias_TPbufferOut);
    printf("  - VTP_coarse:         %d\n",      config->dacs.named.VTP_coarse);
    printf("  - VTP_fine:           %d\n",      config->dacs.named.VTP_fine);
    printf("  - Ibias_CP_PLL:       %d\n",      config->dacs.named.Ibias_CP_PLL);
    printf("  - PLL_Vcntrl:         %d\n",      config->dacs.named.PLL_Vcntrl);
    printf("\n");

    printf("Test Pulses:            %s\n",      config->test_pulse_config.enabled ? "enabled" : "disabled");
    printf("  - Destination:        %s\n",      config->test_pulse_config.digital_only ? "digital" : "analog");
    printf("  - Source:             %s\n",      config->test_pulse_config.external ? "external" : "internal");
    printf("  - Count:              %d\n",      config->test_pulse_config.count);
    printf("  - Period:             %d cc\n",   config->test_pulse_config.period);
    printf("  - Phase:              %d\n",      config->test_pulse_config.phase);

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
 * @param acq Acquisition to initialize
 * @param device Katherine device
 * @param ctx User context (may be used to convey useful info)
 * @param md_buffer_size Size of the measurement data buffer in bytes; at least 65536 on real hardware
 * @param pixel_buffer_size Size of the pixel buffer in bytes
 * @param report_timeout Timeout for reporting incomplete pixel buffers (ms). Set zero to disable.
 * @param fail_timeout Timeout for any device communication (ms). Set zero to disable.
 * @return Error code.
 */
int
katherine_acquisition_init(katherine_acquisition_t *acq, katherine_device_t *device, void *ctx, size_t md_buffer_size, size_t pixel_buffer_size, int report_timeout, int fail_timeout)
{
    int res = 0;

    acq->device       = device;
    acq->user_ctx     = ctx;
    acq->state        = ACQUISITION_NOT_STARTED;
    acq->aborted      = false;
    acq->frame_active = false;

    acq->truncated_measurement_data = 0;

    // Handlers are optional. Clear them so that a caller which registers only
    // some of them does not leave the rest pointing at indeterminate values.
    acq->handlers = (katherine_acquisition_handlers_t) {0};

    acq->md_buffer_size = md_buffer_size;

    // The read loop accesses 6-byte MD's as uint64_t's, so the last MD of a
    // full buffer is read up to 2 bytes beyond the received data. Allocate a
    // whole extra word so that this stays within the allocation for any
    // requested size, including multiples of 8.
    acq->md_buffer = (char *) malloc(md_buffer_size + sizeof(uint64_t));
    if (acq->md_buffer == NULL) {
        res = -KATHERINE_E_NOMEM;
        goto err_datagram_buffer;
    }

    acq->pixel_buffer_size  = pixel_buffer_size;
    acq->pixel_buffer       = (char *) malloc(acq->pixel_buffer_size);
    acq->pixel_buffer_valid = 0;
    if (acq->pixel_buffer == NULL) {
        res = -KATHERINE_E_NOMEM;
        goto err_pixel_buffer;
    }

    acq->report_timeout = report_timeout;
    acq->fail_timeout   = fail_timeout;

    return res;

err_pixel_buffer:
    free(acq->md_buffer);
err_datagram_buffer:
    return res;
}

/**
 * Finalize acquisition
 * @param acq Acquisition to finalize
 */
void
katherine_acquisition_fini(katherine_acquisition_t *acq)
{
    // Prevent a dangling pointer to invalid memory.
    acq->device->acquisition = NULL;

    free(acq->md_buffer);
    free(acq->pixel_buffer);
}

#define DEFINE_ACQ_IMPL(SUFFIX, TAG, MAP) \
    static inline void \
    handle_measurement_data_##SUFFIX##TAG(katherine_acquisition_t *acq, const uint64_t *md) \
    { \
        char hdr = EXTRACT(*md, md, header); \
\
        if (hdr == 0x4) { \
            if (acq->pixel_buffer_valid == acq->pixel_buffer_max_valid) { \
                flush_buffer(acq); \
            } \
\
            MAP((katherine_px_##SUFFIX##_t *) acq->pixel_buffer + acq->pixel_buffer_valid, md, acq); \
            ++acq->pixel_buffer_valid; \
        } else { \
            switch (hdr) { \
            case 0x2: handle_trigger_info(acq, md); break; \
            case 0x3: handle_trigger_info(acq, md); break; \
            case 0x5: handle_timestamp_offset_driven_mode(acq, md); break; \
            case 0x7: handle_new_frame(acq, md); break; \
            case 0x8: handle_frame_start_timestamp_lsb(acq, md); break; \
            case 0x9: handle_frame_start_timestamp_msb(acq, md); break; \
            case 0xA: handle_frame_end_timestamp_lsb(acq, md); break; \
            case 0xB: handle_frame_end_timestamp_msb(acq, md); break; \
            case 0xC: handle_current_frame_finished(acq, md); break; \
            case 0xD: handle_lost_pixel_count(acq, md); break; \
            case 0xE: handle_aborted_measurement(acq, md); break; \
            default:  handle_unknown_msg(acq, md); break; \
            } \
        } \
    } \
\
    static int \
    acquisition_read_##SUFFIX##TAG(katherine_acquisition_t *acq) \
    { \
        static const int PIXEL_SIZE = sizeof(katherine_px_##SUFFIX##_t); \
\
        int lock_res = katherine_udp_mutex_lock(&acq->device->data_socket); \
        if (lock_res != 0) return lock_res; \
\
        time_t last_data_received = time(NULL); \
        double duration; \
        double kill_off_time = acq->fail_timeout <= 0 ? -1 : acq->requested_frames * acq->requested_frame_duration + (double) acq->fail_timeout / 1000.0; \
        int res; \
\
        size_t i; \
        size_t received; \
\
        acq->pixel_buffer_valid     = 0; \
        acq->pixel_buffer_max_valid = acq->pixel_buffer_size / PIXEL_SIZE; \
\
        while (acq->state == ACQUISITION_RUNNING) { \
            received = acq->md_buffer_size; \
            res      = katherine_udp_recv(&acq->device->data_socket, acq->md_buffer, &received); \
\
            if (res) { \
                duration = 1000 * difftime(time(NULL), last_data_received); \
                if (acq->report_timeout > 0 && duration > acq->report_timeout && acq->pixel_buffer_valid > 0) { \
                    flush_buffer(acq); \
                } \
\
                duration = difftime(time(NULL), acq->acq_start_time); \
                if (kill_off_time > 0 && duration > kill_off_time) { \
                    acq->state = ACQUISITION_TIMED_OUT; \
                } \
\
                /* An abort ends the acquisition as soon as the stream has \
                   dried up, whether or not the data are decoded. The flag \
                   is raised by katherine_acquisition_abort() and by the \
                   aborted measurement datum alike. */ \
                if (acq->aborted) { \
                    acq->state = ACQUISITION_SUCCEEDED; \
                } \
                continue; \
            } \
\
            last_data_received = time(NULL); \
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
            if (acq->decode_data) { \
                const char *it = acq->md_buffer; \
                /* Whole data only: the trailing fragment of a datagram cut \
                   short is not a datum, and decoding it would decode the \
                   bytes that happen to follow it in the buffer. */ \
                for (i = 0; i + KATHERINE_MD_SIZE <= received; i += KATHERINE_MD_SIZE, it += KATHERINE_MD_SIZE) { \
                    handle_measurement_data_##SUFFIX##TAG(acq, (const uint64_t *) it); \
                } \
            } else if (acq->handlers.data_received != NULL) { \
                acq->handlers.data_received(acq->user_ctx, acq->md_buffer, received); \
            } \
        } \
\
        if (acq->frame_active) { \
            handle_acquisition_interrupted(acq); \
        } else if (acq->pixel_buffer_valid > 0) { \
            flush_buffer(acq); \
        } \
\
        (void) katherine_udp_mutex_unlock(&acq->device->data_socket); \
        switch (acq->state) { \
        case ACQUISITION_SUCCEEDED: return 0; \
        case ACQUISITION_TIMED_OUT: return -KATHERINE_E_TIMEOUT; \
        /* Reachable only if the read loop above never ran at all, i.e. \
           katherine_acquisition_read() was called on an acquisition that \
           katherine_acquisition_begin() never brought to ACQUISITION_RUNNING: \
           the two other terminal states are the explicit cases above. */ \
        default: return -KATHERINE_E_STATE; \
        } \
    }

/* Timestamp-bearing modes are instantiated once per pixel-clock divider so the
   coarse-to-fine scale is a constant in the decode loop; see md.h. The two
   Event+iToT modes carry no timestamp and so need only one instance each. */
#define DEFINE_ACQ_IMPL_SHIFTED(SUFFIX, SHIFT) DEFINE_ACQ_IMPL(SUFFIX, _s##SHIFT, pmd_##SUFFIX##_s##SHIFT##_map)

#define DEFINE_ACQ_IMPL_EVERY_SHIFT(SUFFIX) \
    DEFINE_ACQ_IMPL_SHIFTED(SUFFIX, 2) \
    DEFINE_ACQ_IMPL_SHIFTED(SUFFIX, 3) \
    DEFINE_ACQ_IMPL_SHIFTED(SUFFIX, 4) \
    DEFINE_ACQ_IMPL_SHIFTED(SUFFIX, 5)

DEFINE_ACQ_IMPL_EVERY_SHIFT(f_toa_tot)
DEFINE_ACQ_IMPL_EVERY_SHIFT(toa_tot)
DEFINE_ACQ_IMPL_EVERY_SHIFT(f_toa_only)
DEFINE_ACQ_IMPL_EVERY_SHIFT(toa_only)

DEFINE_ACQ_IMPL(f_event_itot, , pmd_f_event_itot_map)
DEFINE_ACQ_IMPL(event_itot, , pmd_event_itot_map)

#undef DEFINE_ACQ_IMPL_EVERY_SHIFT
#undef DEFINE_ACQ_IMPL_SHIFTED
#undef DEFINE_ACQ_IMPL

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
 * @param acq Acquisition the pixel was decoded by.
 * @param coord Pixel coordinates.
 * @return Offset applied to this pixel's column, in fine-oscillator ticks.
 */
uint8_t
katherine_acquisition_timestamp_phase_offset(const katherine_acquisition_t *acq, katherine_coord_t coord)
{
    (void) acq;
    (void) coord;

    /* No phase correction is applied yet, so nothing has been added to undo. */
    return 0;
}


/**
 * Read measurement data from acquisition.
 *
 * Returns once the acquisition leaves the running state, whether it finished,
 * timed out or was aborted. The device stops reporting a measurement in flight
 * at that point, so inquiries that a running acquisition refuses -- the sensor
 * temperature among them -- become available again.
 *
 * What ends it differs by path. Decoding, it is the frame-finished datum for
 * the last requested frame. Not decoding, that datum is never examined, so
 * only an abort or this call's own timeout ends it. It is worth noting that
 * any such timeout may be either genuine, leaving the readout measuring, or
 * simply a consequence of receiving no further data past the frame-finished
 * datum (which we do not know arrived because we are not decoding).
 *
 * @param acq Acquisition
 * @return Error code.
 */
int
katherine_acquisition_read(katherine_acquisition_t *acq)
{
    int res;

    /* Two dimensions here, not one: the pixel format, and the pixel-clock
       divider the timestamp decoders are instantiated over. A divider outside
       the set means the acquisition never went through
       katherine_acquisition_begin(), and is refused rather than guessed --
       guessing would misscale every timestamp in the run. */
    switch (acq->acq_mode) {
    case ACQUISITION_MODE_TOA_TOT:
        if (acq->fast_vco_enabled) {
            switch (acq->toa_coarse_tick_to_fine_shift) {
            case 2:  res = acquisition_read_f_toa_tot_s2(acq); break;
            case 3:  res = acquisition_read_f_toa_tot_s3(acq); break;
            case 4:  res = acquisition_read_f_toa_tot_s4(acq); break;
            case 5:  res = acquisition_read_f_toa_tot_s5(acq); break;
            default: res = -KATHERINE_E_INVAL; break;
            }
        } else {
            switch (acq->toa_coarse_tick_to_fine_shift) {
            case 2:  res = acquisition_read_toa_tot_s2(acq); break;
            case 3:  res = acquisition_read_toa_tot_s3(acq); break;
            case 4:  res = acquisition_read_toa_tot_s4(acq); break;
            case 5:  res = acquisition_read_toa_tot_s5(acq); break;
            default: res = -KATHERINE_E_INVAL; break;
            }
        }
        break;

    case ACQUISITION_MODE_ONLY_TOA:
        if (acq->fast_vco_enabled) {
            switch (acq->toa_coarse_tick_to_fine_shift) {
            case 2:  res = acquisition_read_f_toa_only_s2(acq); break;
            case 3:  res = acquisition_read_f_toa_only_s3(acq); break;
            case 4:  res = acquisition_read_f_toa_only_s4(acq); break;
            case 5:  res = acquisition_read_f_toa_only_s5(acq); break;
            default: res = -KATHERINE_E_INVAL; break;
            }
        } else {
            switch (acq->toa_coarse_tick_to_fine_shift) {
            case 2:  res = acquisition_read_toa_only_s2(acq); break;
            case 3:  res = acquisition_read_toa_only_s3(acq); break;
            case 4:  res = acquisition_read_toa_only_s4(acq); break;
            case 5:  res = acquisition_read_toa_only_s5(acq); break;
            default: res = -KATHERINE_E_INVAL; break;
            }
        }
        break;

    case ACQUISITION_MODE_EVENT_ITOT:
        if (acq->fast_vco_enabled) {
            res = acquisition_read_f_event_itot(acq);
        } else {
            res = acquisition_read_event_itot(acq);
        }
        break;

    default:
        res = -KATHERINE_E_INVAL;
        break;
    }

    /* One exit for every mode and every outcome, the aborted and timed-out
       ones included, so the device stops reporting a measurement in flight
       exactly once and in one place. */
    acq->device->acquisition = NULL;
    return res;
}

/**
 * Set detector configuration and begin acquisition.
 *
 * With decode_data false the caller takes on the obligation of ending the
 * acquisition, because nothing in the read loop will: see the field's
 * description in acquisition.h. katherine_acquisition_abort() is how.
 *
 * fast_vco_enabled is refused with -KATHERINE_E_INVAL at any config->freq
 * where fine time stamping is not coherent, since the resulting stream would
 * carry a fine field that cannot be subtracted from its coarse one.
 * katherine_freq_is_fast_vco_supported reports which frequencies qualify, and
 * answers without starting an acquisition. Callers deriving times from the
 * phase count want katherine_actual_phases for a related reason -- the phase
 * enumerators name what is asked for, not what the divider grants.
 *
 * Both refusals here, this one and the data-driven frame count, precede every
 * socket, so a failed begin() has sent nothing and left the device idle.
 *
 * @see katherine_freq_is_fast_vco_supported
 * @see katherine_actual_phases
 *
 * @param acq Acquisition
 * @param config Configuration
 * @param readout_mode Readout mode
 * @param acq_mode Acquisition mode
 * @param fast_vco_enabled Enable fast voltage-controlled oscillators
 * @return Error code.
 */
int
katherine_acquisition_begin(katherine_acquisition_t *acq, const katherine_config_t *config, char readout_mode, katherine_acquisition_mode_t acq_mode, bool fast_vco_enabled, bool decode_data)
{
    int res = 0;

    acq->acq_mode         = acq_mode;
    acq->readout_mode     = readout_mode;
    acq->fast_vco_enabled = fast_vco_enabled;
    acq->decode_data      = decode_data;

#if KATHERINE_DEBUG_ACQ > 0
    dump_config(acq, config);
#endif /* KATHERINE_DEBUG_ACQ */

    if (readout_mode == READOUT_DATA_DRIVEN && config->no_frames > 1) {
        res = -KATHERINE_E_INVAL;
        goto err;
    }

    /* Fine time stamping is not coherent at every frequency; the predicate
       owns the criterion. Refused here rather than passed to the sensor,
       which would answer with a stream whose fine field cannot be subtracted
       from its coarse one. */
    if (fast_vco_enabled && !katherine_freq_is_fast_vco_supported(config->freq)) {
        res = -KATHERINE_E_INVAL;
        goto err;
    }

    res = katherine_configure(acq->device, config);
    if (res) goto err;

    res = katherine_set_seq_readout_start(acq->device, readout_mode);
    if (res) goto err;

    res = katherine_set_acq_mode(acq->device, acq_mode, fast_vco_enabled);
    if (res) goto err;

    acq->state = ACQUISITION_RUNNING;

    acq->completed_frames           = 0;
    acq->requested_frames           = config->no_frames;
    acq->requested_frame_duration   = config->acq_time / 1e9;
    acq->dropped_measurement_data   = 0;
    acq->truncated_measurement_data = 0;

    acq->pixel_buffer_valid            = 0;
    acq->pixel_buffer_max_valid        = 0;
    acq->toa_coarse_tick_to_fine_shift = katherine_tpx3_toa_coarse_tick_to_fine_shift(config->freq);
    acq->last_toa_offset               = KATHERINE_TOA_EPOCH_BIAS(acq);
    acq->frame_active                  = false;

    res = katherine_udp_mutex_lock(&acq->device->control_socket);
    if (res) goto err;

    acq->acq_start_time = time(NULL);

    res = katherine_cmd_start_acquisition(&acq->device->control_socket, readout_mode);
    if (res) goto err_cmd;

    (void) katherine_udp_mutex_unlock(&acq->device->control_socket);

    // This creates user obligation to either stop or read the data.
    acq->device->acquisition = acq;
    return 0;

err_cmd:
    (void) katherine_udp_mutex_unlock(&acq->device->control_socket);
err:
    return res;
}

/**
 * Stop acquisition. No acknowledgement exists for this command; the
 * readout signals the end of the current frame through the measurement
 * data stream instead.
 * @param acq Acquisition
 * @return Error code.
 */
int
katherine_acquisition_stop(katherine_acquisition_t *acq)
{
    int res;

    res = katherine_udp_mutex_lock(&acq->device->control_socket);
    if (res) return res;

    res = katherine_cmd_stop_acquisition(&acq->device->control_socket, acq->readout_mode);
    if (res) goto err;

    (void) katherine_udp_mutex_unlock(&acq->device->control_socket);
    return 0;

err:
    (void) katherine_udp_mutex_unlock(&acq->device->control_socket);
    return res;
}


/**
 * Abort acquisition. This command does not wait for confirmation from
 * the readout and will cause the current frame to end upon receiving.
 *
 * For acquisitions with decode_data == false, this is the only way to achieve
 * orderly termination: it raises the flag that katherine_acquisition_read()
 * acts on once the stream dries up, which is the only thing that will end the
 * read short of its timeout.
 *
 * Note the flag it raises is shared with the readout's own aborted-measurement
 * datum, so an acquisition the hardware abandoned and one the caller stopped
 * both finish as ACQUISITION_SUCCEEDED and cannot be told apart afterwards.
 *
 * @param acq Acquisition
 * @return Error code.
 */
int
katherine_acquisition_abort(katherine_acquisition_t *acq)
{
    int res;

    res = katherine_udp_mutex_lock(&acq->device->control_socket);
    if (res) return res;

    res = katherine_cmd_stop_acquisition(&acq->device->control_socket, acq->readout_mode);
    if (res) goto err;

    (void) katherine_udp_mutex_unlock(&acq->device->control_socket);

    acq->aborted = true;
    return 0;

err:
    (void) katherine_udp_mutex_unlock(&acq->device->control_socket);
    return res;
}

/**
 * Get human-readable description of acquisition status.
 *
 * The tokens are the same style katherine_str_readout_type(),
 * katherine_str_acquisition_mode(), katherine_str_phase() and
 * katherine_str_freq() use (repr.c): lowercase, underscore-separated,
 * stable across releases. Note for callers of the 1.x series: this
 * function used to return "not started" and "timed out" (with a space);
 * it now returns "not_started" and "timed_out" to match.
 *
 * @param status Status to describe
 * @return Null-terminated string.
 */
const char *
katherine_str_acquisition_status(char status)
{
    switch (status) {
    case ACQUISITION_NOT_STARTED: return "not_started";
    case ACQUISITION_SUCCEEDED:   return "succeeded";
    case ACQUISITION_RUNNING:     return "running";
    case ACQUISITION_TIMED_OUT:   return "timed_out";
    default:                      return "unknown";
    }
}
