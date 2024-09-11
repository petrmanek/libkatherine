/* Katherine Control Library
 *
 * This file was created on 29.5.18 by Petr Manek.
 * 
 * Contents of this file are copyrighted and subject to license
 * conditions specified in the LICENSE file located in the top
 * directory.
 */

#pragma once

/**
 * @file
 * @brief Functions related to the data acquisition process.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <katherine/global.h>
#include <katherine/device.h>
#include <katherine/config.h>
#include <katherine/px.h>

#define KATHERINE_MD_SIZE 6

// Uncomment the following line to enable acquisition logging:
// #define KATHERINE_DEBUG_ACQ 2

#ifdef __cplusplus
extern "C" {
#endif

typedef struct katherine_frame_info_time_split {
    uint32_t msb, lsb;
} katherine_frame_info_time_split_t;

typedef union katherine_frame_info_time {
    katherine_frame_info_time_split_t b;
    uint64_t d;
} katherine_frame_info_time_t;

typedef struct katherine_frame_info {
    uint64_t received_pixels;
    uint64_t sent_pixels;
    uint64_t lost_pixels;

    katherine_frame_info_time_t start_time;
    katherine_frame_info_time_t end_time;

    time_t start_time_observed;
    time_t end_time_observed;
} katherine_frame_info_t;

typedef struct katherine_acquisition_handlers {
    void (*pixels_received)(void *, const void *, size_t);
    void (*frame_started)(void *, int);
    void (*frame_ended)(void *, int, bool, const katherine_frame_info_t *);
    void (*data_received)(void *, const char *, size_t);
} katherine_acquisition_handlers_t;

typedef enum katherine_readout_type {
    READOUT_SEQUENTIAL = 0,
    READOUT_DATA_DRIVEN = 1
} katherine_readout_type_t;

typedef enum katherine_acquisition_state {
    ACQUISITION_NOT_STARTED = 0,
    ACQUISITION_RUNNING = 1,
    ACQUISITION_SUCCEEDED = 2,
    ACQUISITION_TIMED_OUT = 3
} katherine_acquisition_state_t;

typedef struct katherine_acquisition {
    katherine_device_t *device;
    void *user_ctx;

    char state;
    bool aborted;
    char readout_mode;
    char acq_mode;
    bool fast_vco_enabled;

    char *md_buffer;
    size_t md_buffer_size;

    bool decode_data;
    char *pixel_buffer;
    size_t pixel_buffer_size;
    size_t pixel_buffer_valid;
    size_t pixel_buffer_max_valid;

    int requested_frames;
    double requested_frame_duration; // s
    int completed_frames;
    size_t dropped_measurement_data;

    time_t acq_start_time;
    int report_timeout;
    int fail_timeout;

    katherine_acquisition_handlers_t handlers;
    katherine_frame_info_t current_frame_info;

    uint64_t last_toa_offset;

} katherine_acquisition_t;

KATHERINE_EXPORTED int
katherine_acquisition_init(katherine_acquisition_t *acq, katherine_device_t *device, void *ctx, size_t md_buffer_size, size_t pixel_buffer_size, int report_timeout, int fail_timeout);

KATHERINE_EXPORTED void
katherine_acquisition_fini(katherine_acquisition_t *acq);

KATHERINE_EXPORTED int
katherine_acquisition_begin(katherine_acquisition_t *acq, const katherine_config_t *config, char readout_mode, katherine_acquisition_mode_t acq_mode, bool fast_vco_enabled, bool decode_data);

KATHERINE_EXPORTED int
katherine_acquisition_abort(katherine_acquisition_t *acq);

KATHERINE_EXPORTED int
katherine_acquisition_read(katherine_acquisition_t *acq);

KATHERINE_EXPORTED const char *
katherine_str_acquisition_status(char status);

#ifdef __cplusplus
}
#endif
