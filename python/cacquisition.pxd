# Katherine Control Library
#
# This file was created on 28.2.19 by Petr Manek.
# 
# Contents of this file are copyrighted and subject to license
# conditions specified in the LICENSE file located in the top
# directory.

from libcpp cimport bool
from libc.time cimport time_t
from libc.stdint cimport uint8_t, uint16_t, uint32_t, uint64_t
from cdevice cimport katherine_device_t
from cconfig cimport katherine_config_t, katherine_acquisition_mode_t

cdef extern from 'katherine/acquisition.h':
    ctypedef struct katherine_coord_t:
        uint8_t x
        uint8_t y

    ctypedef struct katherine_px_f_toa_tot_t:
        katherine_coord_t coord
        uint8_t ftoa
        uint64_t toa
        uint16_t tot

    ctypedef struct katherine_px_toa_tot_t:
        katherine_coord_t coord
        uint64_t toa
        uint8_t hit_count
        uint16_t tot
        
    ctypedef struct katherine_px_f_toa_only_t:
        katherine_coord_t coord
        uint8_t ftoa
        uint64_t toa

    ctypedef struct katherine_px_toa_only_t:
        katherine_coord_t coord
        uint64_t toa
        uint8_t hit_count

    ctypedef struct katherine_px_f_event_itot_t:
        katherine_coord_t coord
        uint8_t hit_count
        uint16_t event_count
        uint16_t integral_tot
        
    ctypedef struct katherine_px_event_itot_t:
        katherine_coord_t coord
        uint16_t event_count
        uint16_t integral_tot

    ctypedef struct katherine_frame_info_time_split_t:
        uint32_t msb
        uint32_t lsb

    ctypedef union katherine_frame_info_time_t:
        katherine_frame_info_time_split_t b
        uint64_t d

    ctypedef struct katherine_frame_info_t:
        uint64_t received_pixels
        uint64_t sent_pixels
        uint64_t lost_pixels
        katherine_frame_info_time_t start_time
        katherine_frame_info_time_t end_time
        time_t start_time_observed
        time_t end_time_observed

    ctypedef struct katherine_acquisition_handlers_t:
        void (*pixels_received)(void *, const void *, size_t)
        void (*frame_started)(void *, int)
        void (*frame_ended)(void *, int, bool, const katherine_frame_info_t *)

    ctypedef struct katherine_acquisition_t:
        char state
        bool aborted
        int requested_frames
        int completed_frames
        size_t dropped_measurement_data
        katherine_acquisition_handlers_t handlers

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
    int katherine_acquisition_begin(katherine_acquisition_t *acq, const katherine_config_t *config, char readout_mode, katherine_acquisition_mode_t acq_mode, bool fast_vco_enabled)
    int katherine_acquisition_abort(katherine_acquisition_t *acq)
    int katherine_acquisition_read(katherine_acquisition_t *acq)
    int katherine_str_acquisition_status(char status)
