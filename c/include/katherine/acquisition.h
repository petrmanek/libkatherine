//
// Created by petr on 29.5.18.
//

#ifndef THESIS_ACQUISITION_H
#define THESIS_ACQUISITION_H

/**
 * @file
 * @brief Functions related to the data acquisition process.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <katherine/device.h>
#include <katherine/config.h>

#define KATHERINE_MD_SIZE 6

#ifdef __cplusplus
extern "C" {
#endif

typedef struct katherine_coord {
    uint8_t x;
    uint8_t y;
} katherine_coord_t;

typedef struct katherine_px_f_toa_tot {
    katherine_coord_t coord;
    uint8_t ftoa;
    uint64_t toa;
    uint16_t tot;
} katherine_px_f_toa_tot_t;

typedef struct katherine_px_toa_tot {
    katherine_coord_t coord;
    uint64_t toa;
    uint8_t hit_count;
    uint16_t tot;
} katherine_px_toa_tot_t;

typedef struct katherine_px_f_toa_only {
    katherine_coord_t coord;
    uint8_t ftoa;
    uint64_t toa;
} katherine_px_f_toa_only_t;

typedef struct katherine_px_toa_only {
    katherine_coord_t coord;
    uint64_t toa;
    uint8_t hit_count;
} katherine_px_toa_only_t;

typedef struct katherine_px_f_event_itot {
    katherine_coord_t coord;
    uint8_t hit_count;
    uint16_t event_count;
    uint16_t integral_tot;
} katherine_px_f_event_itot_t;

typedef struct katherine_px_event_itot {
    katherine_coord_t coord;
    uint16_t event_count;
    uint16_t integral_tot;
} katherine_px_event_itot_t;

typedef struct katherine_frame_info {
    uint64_t received_pixels;
    uint64_t sent_pixels;
    uint64_t lost_pixels;

    union {
        struct {
            uint32_t msb, lsb;
        } b;
        uint64_t d;
    } start_time;

    union {
        struct {
            uint32_t msb, lsb;
        } b;
        uint64_t d;
    } end_time;
} katherine_frame_info_t;

typedef struct katherine_acquisition_handlers {
    void (*pixels_received)(const void *, size_t);
    void (*frame_started)(int);
    void (*frame_ended)(int, bool, const katherine_frame_info_t *);
} katherine_acquisition_handlers_t;

typedef enum katherine_readout_type {
    READOUT_SEQUENTIAL = 0,
    READOUT_DATA_DRIVEN = 1
} katherine_readout_type_t;

typedef enum katherine_acquisition_state {
    ACQUISITION_NOT_STARTED = 0,
    ACQUISITION_RUNNING = 1,
    ACQUISITION_SUCCEEDED = 2,
    ACQUISITION_TIMED_OUT = 3,
    ACQUISITION_ABORTED = 4
} katherine_acquisition_state_t;

typedef struct katherine_acquisition {
    katherine_device_t *device;

    char state;
    char readout_mode;
    char acq_mode;
    bool fast_vco_enabled;

    void *md_buffer;
    size_t md_buffer_size;

    void *pixel_buffer;
    size_t pixel_buffer_size;
    size_t pixel_buffer_valid;
    size_t pixel_buffer_max_valid;

    int requested_frames;
    int completed_frames;
    size_t dropped_measurement_data;

    katherine_acquisition_handlers_t handlers;
    katherine_frame_info_t current_frame_info;

    uint64_t last_toa_offset;

} katherine_acquisition_t;

int
katherine_acquisition_init(katherine_acquisition_t *, katherine_device_t *, size_t md_buffer_size, size_t pixel_buffer_size);

void
katherine_acquisition_fini(katherine_acquisition_t *);

int
katherine_acquisition_begin(katherine_acquisition_t *, const katherine_config_t *, char, katherine_acquisition_mode_t, bool);

int
katherine_acquisition_abort(katherine_acquisition_t *);

int
katherine_acquisition_read(katherine_acquisition_t *);

const char *
katherine_str_acquisition_status(char);

#ifdef __cplusplus
}
#endif

#endif //THESIS_ACQUISITION_H
