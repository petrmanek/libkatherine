# Cython declarations for katherine/config.h.
# Created 28.2.19 by Petr Mánek.
#
# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

from libcpp cimport bool
from libc.stdint cimport uint8_t, uint16_t, int32_t
from cdevice cimport katherine_device_t
from cpx_config cimport katherine_px_config_t

cdef extern from 'katherine/config.h':
    ctypedef enum katherine_tpx3_px_mode_t:
        KATHERINE_TPX3_PX_TOA_TOT
        KATHERINE_TPX3_PX_ONLY_TOA
        KATHERINE_TPX3_PX_EVENT_COUNT_ITOT

    ctypedef struct katherine_trigger_t:
        bool enabled
        char channel
        bool use_falling_edge

    int katherine_trigger_snprint(char *buf, size_t cap, const katherine_trigger_t *v)

    ctypedef struct katherine_test_pulse_config_t:
        bool enabled
        bool digital_only
        bool external
        uint16_t count
        uint16_t period
        uint8_t phase

    int katherine_test_pulse_config_snprint(char *buf, size_t cap, const katherine_test_pulse_config_t *v)

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

    int katherine_dacs_snprint(char *buf, size_t cap, const katherine_dacs_t *v)

    ctypedef enum katherine_polarity_t:
        KATHERINE_POLARITY_HOLES
        KATHERINE_POLARITY_ELECTRONS

    const char *katherine_str_polarity(katherine_polarity_t polarity)

    ctypedef enum katherine_tpx3_phase_t:
        KATHERINE_TPX3_PHASE_1
        KATHERINE_TPX3_PHASE_2
        KATHERINE_TPX3_PHASE_4
        KATHERINE_TPX3_PHASE_8
        KATHERINE_TPX3_PHASE_16

    ctypedef enum katherine_tpx3_freq_t:
        KATHERINE_TPX3_FREQ_20_MHZ
        KATHERINE_TPX3_FREQ_40_MHZ
        KATHERINE_TPX3_FREQ_80_MHZ
        KATHERINE_TPX3_FREQ_160_MHZ

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
        katherine_polarity_t polarity

        bool correct_phase

        katherine_tpx3_phase_t phase
        katherine_tpx3_freq_t freq
        katherine_dacs_t dacs

        katherine_test_pulse_config_t test_pulse_config

    int katherine_config_snprint(char *buf, size_t cap, const katherine_config_t *v)

    ctypedef enum katherine_tpx3_reg_t:
        KATHERINE_TPX3_REG_TEST_PULSE_METHOD
        KATHERINE_TPX3_REG_NUMBER_TEST_PULSES
        KATHERINE_TPX3_REG_OUT_BLOCK_CONFIG
        KATHERINE_TPX3_REG_PLL_CONFIG
        KATHERINE_TPX3_REG_GENERAL_CONFIG
        KATHERINE_TPX3_REG_SLVS_CONFIG
        KATHERINE_TPX3_REG_POWER_PULSING_PATTERN
        KATHERINE_TPX3_REG_SET_TIMER_LOW
        KATHERINE_TPX3_REG_SET_TIMER_MID
        KATHERINE_TPX3_REG_SET_TIMER_HIGH
        KATHERINE_TPX3_REG_SENSE_DAC_SELECTOR
        KATHERINE_TPX3_REG_EXT_DAC_SELECTOR

    int katherine_configure(katherine_device_t *device, const katherine_config_t *config)
    int katherine_set_all_pixel_config(katherine_device_t *device, const katherine_px_config_t *px_config)
    int katherine_set_acq_time(katherine_device_t *device, double ns)
    int katherine_set_acq_mode(katherine_device_t *device, katherine_tpx3_px_mode_t px_mode, bool fast_vco_enabled)
    int katherine_set_no_frames(katherine_device_t *device, int no_frames)
    int katherine_set_bias(katherine_device_t *device, unsigned char bias_id, float bias_value)
    int katherine_set_seq_readout_start(katherine_device_t *device, int arg)
    int katherine_acquisition_setup(katherine_device_t *device, const katherine_trigger_t *start_trigger, bool delayed_start, const katherine_trigger_t *end_trigger)
    int katherine_set_sensor_register(katherine_device_t *device, char reg_idx, int32_t reg_value)
    int katherine_update_sensor_registers(katherine_device_t *device)
    int katherine_output_block_config_update(katherine_device_t *device)
    int katherine_timer_set(katherine_device_t *device)
    int katherine_set_dacs(katherine_device_t *device, const katherine_dacs_t *dacs)
    int katherine_set_test_pulses(katherine_device_t *device, const katherine_test_pulse_config_t *test_pulse_config)
