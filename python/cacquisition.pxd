# Cython declarations for katherine/acquisition.h.
# Created 28.2.19 by Petr Mánek.
#
# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

from libcpp cimport bool
from libc.time cimport time_t
from libc.stdint cimport uint32_t, uint64_t
from cdevice cimport katherine_device_t
from cconfig cimport katherine_config_t, katherine_acquisition_mode_t

cdef extern from 'katherine/acquisition.h':
    ctypedef struct katherine_frame_info_time_split_t:
        uint32_t lsb
        uint32_t msb

    ctypedef union katherine_frame_info_time_t:
        katherine_frame_info_time_split_t b
        uint64_t d

    int katherine_frame_info_time_snprint(char *buf, size_t cap, const katherine_frame_info_time_t *v)

    ctypedef struct katherine_frame_info_t:
        uint64_t received_pixels
        uint64_t sent_pixels
        uint64_t lost_pixels
        katherine_frame_info_time_t start_time
        katherine_frame_info_time_t end_time
        time_t start_time_observed
        time_t end_time_observed
        bool completed

    int katherine_frame_info_snprint(char *buf, size_t cap, const katherine_frame_info_t *v)

    ctypedef struct katherine_acquisition_handlers_t:
        void (*pixels_received)(void *, const void *, size_t)
        void (*frame_started)(void *, int)
        void (*frame_ended)(void *, int, bool, const katherine_frame_info_t *)
        void (*data_received)(void *, const char *, size_t)

    ctypedef struct katherine_acquisition_t:
        katherine_device_t *device
        void *user_ctx

        char state
        bool aborted
        char readout_mode
        char acq_mode
        bool fast_vco_enabled

        char *md_buffer
        size_t md_buffer_size

        bool decode_data
        char *pixel_buffer
        size_t pixel_buffer_size
        size_t pixel_buffer_valid
        size_t pixel_buffer_max_valid

        int requested_frames
        double requested_frame_duration
        int completed_frames
        size_t dropped_measurement_data
        uint64_t truncated_measurement_data

        time_t acq_start_time
        int report_timeout
        int fail_timeout

        katherine_acquisition_handlers_t handlers
        katherine_frame_info_t current_frame_info

        uint64_t last_toa_offset

        bool frame_active

    int katherine_acquisition_snprint(char *buf, size_t cap, const katherine_acquisition_t *v)

    ctypedef enum katherine_readout_type_t:
        READOUT_SEQUENTIAL
        READOUT_DATA_DRIVEN

    ctypedef enum katherine_acquisition_state_t:
        ACQUISITION_NOT_STARTED
        ACQUISITION_RUNNING
        ACQUISITION_SUCCEEDED
        ACQUISITION_TIMED_OUT

    cdef int KATHERINE_MD_SIZE

    int katherine_acquisition_init(katherine_acquisition_t *acq, katherine_device_t *device, void *ctx, size_t md_buffer_size, size_t pixel_buffer_size, int report_timeout, int fail_timeout)
    void katherine_acquisition_fini(katherine_acquisition_t *acq)
    int katherine_acquisition_begin(katherine_acquisition_t *acq, const katherine_config_t *config, char readout_mode, katherine_acquisition_mode_t acq_mode, bool fast_vco_enabled, bool decode_data)
    int katherine_acquisition_abort(katherine_acquisition_t *acq)
    int katherine_acquisition_stop(katherine_acquisition_t *acq)
    int katherine_acquisition_read(katherine_acquisition_t *acq)
    const char *katherine_str_acquisition_status(char status)
