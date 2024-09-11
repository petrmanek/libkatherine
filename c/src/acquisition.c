/* Katherine Control Library
 *
 * This file was created on 29.5.18 by Petr Manek.
 * 
 * Contents of this file are copyrighted and subject to license
 * conditions specified in the LICENSE file located in the top
 * directory.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <katherine/global.h>
#include <katherine/acquisition.h>
#include "command_interface.h"
#include "md.h"

#ifndef DOXYGEN_SHOULD_SKIP_THIS

static inline void
flush_buffer(katherine_acquisition_t *acq)
{
    acq->handlers.pixels_received(acq->user_ctx, acq->pixel_buffer, acq->pixel_buffer_valid);

    acq->current_frame_info.received_pixels += acq->pixel_buffer_valid;
    acq->pixel_buffer_valid = 0;
}

static inline void
handle_new_frame(katherine_acquisition_t *acq, const uint64_t *data)
{
    memset(&acq->current_frame_info, 0, sizeof(katherine_frame_info_t));
    acq->current_frame_info.start_time_observed = time(NULL);
    acq->handlers.frame_started(acq->user_ctx, acq->completed_frames);
}

static inline void
handle_timestamp_offset_driven_mode(katherine_acquisition_t *acq, const uint64_t *data)
{
    acq->last_toa_offset = 16384 * EXTRACT(*data, md_time_offset, offset);
}

static inline void
handle_current_frame_finished(katherine_acquisition_t *acq, const uint64_t *data)
{
    acq->current_frame_info.end_time_observed = time(NULL);

    flush_buffer(acq);

    acq->current_frame_info.sent_pixels = EXTRACT(*data, md_frame_finished, n_sent);
    acq->handlers.frame_ended(acq->user_ctx, acq->completed_frames, true, &acq->current_frame_info);

    ++acq->completed_frames;

    if (acq->completed_frames == acq->requested_frames) {
        acq->state = ACQUISITION_SUCCEEDED;
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
    ++acq->dropped_measurement_data;
}

#ifdef KATHERINE_DEBUG_ACQ
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

    printf("----  End Acquisition Configuration  ----\n");
}
#endif /* KATHERINE_DEBUG_ACQ */

#endif /* DOXYGEN_SHOULD_SKIP_THIS */

/**
 * Initialize acquisition.
 * @param acq Acquisition to initialize
 * @param device Katherine device
 * @param ctx User context (may be used to convey useful info)
 * @param md_buffer_size Size of the measurement data buffer in bytes
 * @param pixel_buffer_size Size of the pixel buffer in bytes
 * @param report_timeout Timeout for reporting incomplete pixel buffers (ms). Set zero to disable.
 * @param fail_timeout Timeout for any device communication (ms). Set zero to disable.
 * @return Error code.
 */
int
katherine_acquisition_init(katherine_acquisition_t *acq, katherine_device_t *device, void *ctx, size_t md_buffer_size, size_t pixel_buffer_size, int report_timeout, int fail_timeout)
{
    int res = 0;

    acq->device = device;
    acq->user_ctx = ctx;
    acq->state = ACQUISITION_NOT_STARTED;
    acq->aborted = false;

    acq->md_buffer_size = md_buffer_size;

    // Round MD buffer size up to the nearest multiple of 8 bytes.
    // This is just a safety precaution due to accessing 6-byte MD's as uint64_t's.
    size_t actual_md_buffer_size = ((acq->md_buffer_size + 7) / 8) * 8;
    acq->md_buffer = (char *) malloc(actual_md_buffer_size);
    if (acq->md_buffer == NULL) {
        res = ENOMEM;
        goto err_datagram_buffer;
    }

    acq->pixel_buffer_size = pixel_buffer_size;
    acq->pixel_buffer = (char *) malloc(acq->pixel_buffer_size);
    acq->pixel_buffer_valid = 0;
    if (acq->pixel_buffer == NULL) {
        res = ENOMEM;
        goto err_pixel_buffer;
    }

    acq->report_timeout = report_timeout;
    acq->fail_timeout = fail_timeout;

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
    free(acq->md_buffer);
    free(acq->pixel_buffer);
}

#define DEFINE_ACQ_IMPL(SUFFIX) \
    static inline void\
    handle_measurement_data_##SUFFIX(katherine_acquisition_t *acq, const uint64_t *md)\
    {\
        static const int PIXEL_SIZE = sizeof(katherine_px_##SUFFIX##_t);\
        char hdr = EXTRACT(*md, md, header);\
        \
        if (hdr == 0x4) {\
            if (acq->pixel_buffer_valid == acq->pixel_buffer_max_valid) {\
                flush_buffer(acq);\
            }\
            \
            pmd_##SUFFIX##_map((katherine_px_##SUFFIX##_t *) acq->pixel_buffer + acq->pixel_buffer_valid, md, acq);\
            ++acq->pixel_buffer_valid;\
        } else {\
            switch (hdr) {\
            case 0x2: handle_trigger_info(acq, md); break;\
            case 0x3: handle_trigger_info(acq, md); break;\
            case 0x5: handle_timestamp_offset_driven_mode(acq, md); break;\
            case 0x7: handle_new_frame(acq, md); break;\
            case 0x8: handle_frame_start_timestamp_lsb(acq, md); break;\
            case 0x9: handle_frame_start_timestamp_msb(acq, md); break;\
            case 0xA: handle_frame_end_timestamp_lsb(acq, md); break;\
            case 0xB: handle_frame_end_timestamp_msb(acq, md); break;\
            case 0xC: handle_current_frame_finished(acq, md); break;\
            case 0xD: handle_lost_pixel_count(acq, md); break;\
            case 0xE: handle_aborted_measurement(acq, md); break;\
            default:  handle_unknown_msg(acq, md); break;\
            }\
        }\
    }\
    \
    static int\
    acquisition_read_##SUFFIX(katherine_acquisition_t *acq)\
    {\
        static const int PIXEL_SIZE = sizeof(katherine_px_##SUFFIX##_t);\
        \
        if (katherine_udp_mutex_lock(&acq->device->data_socket) != 0) return 1;\
        \
        time_t last_data_received = time(NULL);\
        double duration;\
        double kill_off_time = acq->fail_timeout <= 0 ? -1 : acq->requested_frames * acq->requested_frame_duration + (double) acq->fail_timeout / 1000.0;\
        int res;\
        \
        size_t i;\
        size_t received;\
        \
        acq->pixel_buffer_valid = 0;\
        acq->pixel_buffer_max_valid = acq->pixel_buffer_size / PIXEL_SIZE;\
        \
        while (acq->state == ACQUISITION_RUNNING) {\
            received = acq->md_buffer_size;\
            res = katherine_udp_recv(&acq->device->data_socket, acq->md_buffer, &received);\
            \
            if (res) {\
                duration = 1000 * difftime(time(NULL), last_data_received);\
                if (acq->report_timeout > 0 && duration > acq->report_timeout && acq->pixel_buffer_valid > 0) {\
                    flush_buffer(acq);\
                }\
                \
                duration = difftime(time(NULL), acq->acq_start_time);\
                if (kill_off_time > 0 && duration > kill_off_time) {\
                    acq->state = ACQUISITION_TIMED_OUT;\
                }\
		\
		if(!acq->decode_data && acq->aborted) {		\
		  acq->state = ACQUISITION_SUCCEEDED;\
		}				     \
                \
                continue;\
            }\
            \
            last_data_received = time(NULL);\
            \
	    if(acq->decode_data) {\
	        const char *it = acq->md_buffer;\
                for (i = 0; i < received; i += KATHERINE_MD_SIZE, it += KATHERINE_MD_SIZE) { \
                    handle_measurement_data_##SUFFIX(acq, (const uint64_t *) it);\
                }\
	    } else {\
                acq->handlers.data_received(acq->user_ctx, acq->md_buffer, received);\
	    }\
        }\
        \
        (void) katherine_udp_mutex_unlock(&acq->device->data_socket);\
        switch (acq->state) {\
        case ACQUISITION_SUCCEEDED:     return 0;\
        case ACQUISITION_TIMED_OUT:     return ETIMEDOUT;\
        default:                        return EAGAIN;\
        }\
    }

DEFINE_ACQ_IMPL(f_toa_tot);
DEFINE_ACQ_IMPL(toa_tot);
DEFINE_ACQ_IMPL(f_toa_only);
DEFINE_ACQ_IMPL(toa_only);
DEFINE_ACQ_IMPL(f_event_itot);
DEFINE_ACQ_IMPL(event_itot);

#undef DEFINE_ACQ_IMPL

/**
 * Read measurement data from acquisition.
 * @param acq Acquisition
 * @return Error code.
 */
int
katherine_acquisition_read(katherine_acquisition_t *acq)
{
    switch (acq->acq_mode) {
    case ACQUISITION_MODE_TOA_TOT:
        if (acq->fast_vco_enabled) {
            return acquisition_read_f_toa_tot(acq);
        } else {
            return acquisition_read_toa_tot(acq);
        }

    case ACQUISITION_MODE_ONLY_TOA:
        if (acq->fast_vco_enabled) {
            return acquisition_read_f_toa_only(acq);
        } else {
            return acquisition_read_toa_only(acq);
        }

    case ACQUISITION_MODE_EVENT_ITOT:
        if (acq->fast_vco_enabled) {
            return acquisition_read_f_event_itot(acq);
        } else {
            return acquisition_read_event_itot(acq);
        }

    default:
        return EINVAL;
    }
}

/**
 * Set detector configuration and begin acquisition.
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

    acq->acq_mode = acq_mode;
    acq->readout_mode = readout_mode;
    acq->fast_vco_enabled = fast_vco_enabled;
    acq->decode_data = decode_data;

#if KATHERINE_DEBUG_ACQ > 0
    dump_config(acq, config);
#endif /* KATHERINE_DEBUG_ACQ */

    if (readout_mode == READOUT_DATA_DRIVEN && config->no_frames > 1) {
        res = EINVAL;
        goto err;
    }

    res = katherine_configure(acq->device, config);
    if (res) goto err;

    res = katherine_set_seq_readout_start(acq->device, readout_mode);
    if (res) goto err;

    res = katherine_set_acq_mode(acq->device, acq_mode, fast_vco_enabled);
    if (res) goto err;

    acq->state = ACQUISITION_RUNNING;

    acq->completed_frames = 0;
    acq->requested_frames = config->no_frames;
    acq->requested_frame_duration = config->acq_time / 1e9;
    acq->dropped_measurement_data = 0;

    acq->pixel_buffer_valid = 0;
    acq->pixel_buffer_max_valid = 0;
    acq->last_toa_offset = 0;

    res = katherine_udp_mutex_lock(&acq->device->control_socket);
    if (res) goto err;

    acq->acq_start_time = time(NULL);

    res = katherine_cmd_start_acquisition(&acq->device->control_socket, readout_mode);
    if (res) goto err_cmd;

    (void) katherine_udp_mutex_unlock(&acq->device->control_socket);
    return 0;

err_cmd:
    (void) katherine_udp_mutex_unlock(&acq->device->control_socket);
err:
    return res;
}

/**
 * Abort acquisition. This command does not wait for confirmation from
 * the readout and will cause the current frame to end upon receiving.
 * @param acq Acquisition
 * @return Error code.
 */
int
katherine_acquisition_abort(katherine_acquisition_t *acq)
{
    int res;

    res = katherine_udp_mutex_lock(&acq->device->control_socket);
    if (res) goto err;

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
 * @param status Status to describe
 * @return Null-terminated string.
 */
const char*
katherine_str_acquisition_status(char status)
{
    switch (status) {
    case ACQUISITION_NOT_STARTED:   return "not started";
    case ACQUISITION_SUCCEEDED:     return "succeeded";
    case ACQUISITION_RUNNING:       return "running";
    case ACQUISITION_TIMED_OUT:     return "timed out";
    default:                        return "unknown";
    }
}
