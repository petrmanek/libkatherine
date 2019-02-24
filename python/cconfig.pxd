# Katherine Control Library
#
# This file was created on 28.2.19 by Petr Manek.
# 
# Contents of this file are copyrighted and subject to license
# conditions specified in the LICENSE file located in the top
# directory.

from libcpp cimport bool
from libc.stdint cimport uint16_t
from cpx_config cimport katherine_px_config_t

cdef extern from 'katherine/config.h':
    ctypedef enum katherine_acquisition_mode_t:
        ACQUISITION_MODE_TOA_TOT
        ACQUISITION_MODE_ONLY_TOA
        ACQUISITION_MODE_EVENT_ITOT

    ctypedef struct katherine_trigger_t:
        bool enabled
        char channel
        bool use_falling_edge

    ctypedef struct katherine_dacs_named_t:
        uint16_t Ibias_Preamp_ON
        uint16_t Ibias_Preamp_OFF
        uint16_t VPReamp_NCAS
        uint16_t Ibias_Ikrum
        uint16_t Vfbk
        uint16_t Vthreshold_fine
        uint16_t Vthreshold_coarse
        uint16_t Ibias_DiscS1_ON
        uint16_t Ibias_DiscS1_OFF
        uint16_t Ibias_DiscS2_ON
        uint16_t Ibias_DiscS2_OFF
        uint16_t Ibias_PixelDAC
        uint16_t Ibias_TPbufferIn
        uint16_t Ibias_TPbufferOut
        uint16_t VTP_coarse
        uint16_t VTP_fine
        uint16_t Ibias_CP_PLL
        uint16_t PLL_Vcntrl

    ctypedef union katherine_dacs_t:
        uint16_t array[18]
        katherine_dacs_named_t named

    ctypedef enum katherine_phase_t:
        PHASE_1
        PHASE_2
        PHASE_4
        PHASE_8
        PHASE_16   

    ctypedef enum katherine_freq_t:
        FREQ_40
        FREQ_80
        FREQ_160        

    ctypedef struct katherine_config_t:
        katherine_px_config_t pixel_config

        unsigned char bias_id

        double acq_time
        int no_frames

        float bias
        katherine_trigger_t start_trigger
        bool delayed_start
        katherine_trigger_t stop_trigger

        bool gray_disable
        bool polarity_holes

        katherine_phase_t phase
        katherine_freq_t freq
        katherine_dacs_t dacs
