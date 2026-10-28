# Cython declarations for katherine/config.h.
# Created 28.2.19 by Petr Mánek.
#
# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

from libcpp cimport bool
from libc.stdint cimport uint8_t, uint16_t, uint32_t, int32_t
from cdevice cimport katherine_device_t, katherine_chip_type_t
from cpx_config cimport katherine_tpx3_px_config_t

cdef extern from 'katherine/config.h':
    ctypedef enum katherine_tpx3_px_mode_t:
        KATHERINE_TPX3_PX_TOA_TOT
        KATHERINE_TPX3_PX_ONLY_TOA
        KATHERINE_TPX3_PX_EVENT_COUNT_ITOT

    ctypedef enum katherine_trigger_edge_t:
        KATHERINE_TRIGGER_EDGE_RISING
        KATHERINE_TRIGGER_EDGE_FALLING

    const char *katherine_str_trigger_edge(katherine_trigger_edge_t edge)

    ctypedef enum katherine_trigger_source_t:
        KATHERINE_TRIGGER_SOURCE_PULSE_INTERNAL
        KATHERINE_TRIGGER_SOURCE_GPIO_0
        KATHERINE_TRIGGER_SOURCE_GPIO_1
        KATHERINE_TRIGGER_SOURCE_GPIO_2
        KATHERINE_TRIGGER_SOURCE_GPIO_3
        KATHERINE_TRIGGER_SOURCE_GPIO_4
        KATHERINE_TRIGGER_SOURCE_GPIO_5
        KATHERINE_TRIGGER_SOURCE_GPIO_6
        KATHERINE_TRIGGER_SOURCE_GPIO_7

    ctypedef struct katherine_trigger_t:
        bool use_trigger
        katherine_trigger_source_t channel
        katherine_trigger_edge_t edge
        bool delayed
        double delay_seconds

    size_t KATHERINE_TPX3_CONFIG_SIZE(uint8_t chip_count)

    int katherine_trigger_snprint(char *buf, size_t cap, const katherine_trigger_t *v)

    ctypedef struct katherine_tpx3_test_pulse_config_t:
        bool enabled
        bool digital_only
        bool external
        uint16_t count
        uint16_t period
        uint8_t phase

    int katherine_tpx3_test_pulse_config_snprint(char *buf, size_t cap, const katherine_tpx3_test_pulse_config_t *v)

    ctypedef struct katherine_tpx3_dacs_named_t:
        uint16_t Ibias_Preamp_ON
        uint16_t Ibias_Preamp_OFF
        uint16_t Vpreamp_NCAS
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

    ctypedef union katherine_tpx3_dacs_t:
        uint16_t array[18]
        katherine_tpx3_dacs_named_t named

    int katherine_tpx3_dacs_snprint(char *buf, size_t cap, const katherine_tpx3_dacs_t *v)

    cdef int KATHERINE_TPX3_DAC_MONITOR_COUNT
    cdef int KATHERINE_CONFIG_MAX_BIAS_SUPPLIES
    cdef int KATHERINE_TPX3_DAC_COUNT

    ctypedef enum katherine_tpx3_dac_t:
        KATHERINE_TPX3_DAC_IBIAS_PREAMP_ON
        KATHERINE_TPX3_DAC_IBIAS_PREAMP_OFF
        KATHERINE_TPX3_DAC_VPREAMP_NCAS
        KATHERINE_TPX3_DAC_IBIAS_IKRUM
        KATHERINE_TPX3_DAC_VFBK
        KATHERINE_TPX3_DAC_VTHRESHOLD_FINE
        KATHERINE_TPX3_DAC_VTHRESHOLD_COARSE
        KATHERINE_TPX3_DAC_IBIAS_DISCS1_ON
        KATHERINE_TPX3_DAC_IBIAS_DISCS1_OFF
        KATHERINE_TPX3_DAC_IBIAS_DISCS2_ON
        KATHERINE_TPX3_DAC_IBIAS_DISCS2_OFF
        KATHERINE_TPX3_DAC_IBIAS_PIXELDAC
        KATHERINE_TPX3_DAC_IBIAS_TPBUFFERIN
        KATHERINE_TPX3_DAC_IBIAS_TPBUFFEROUT
        KATHERINE_TPX3_DAC_VTP_COARSE
        KATHERINE_TPX3_DAC_VTP_FINE
        KATHERINE_TPX3_DAC_IBIAS_CP_PLL
        KATHERINE_TPX3_DAC_PLL_VCNTRL

    ctypedef enum katherine_dac_unit_t:
        KATHERINE_DAC_UNIT_AMP
        KATHERINE_DAC_UNIT_VOLT

    const char *katherine_tpx3_dac_name(katherine_tpx3_dac_t dac)
    uint16_t katherine_tpx3_dac_max(katherine_tpx3_dac_t dac)
    double katherine_tpx3_dac_to_si(katherine_tpx3_dac_t dac, uint16_t value, katherine_dac_unit_t *unit)

    ctypedef enum katherine_tpx3_dac_monitor_t:
        KATHERINE_TPX3_DAC_MONITOR_BANDGAP_OUTPUT
        KATHERINE_TPX3_DAC_MONITOR_BANDGAP_TEMP
        KATHERINE_TPX3_DAC_MONITOR_IBIAS_DAC
        KATHERINE_TPX3_DAC_MONITOR_IBIAS_DAC_CAS

    ctypedef union katherine_tpx3_dac_voltages_t:
        float array[22]

    int katherine_tpx3_dac_voltages_snprint(char *buf, size_t cap, const katherine_tpx3_dac_voltages_t *v)
    const char *katherine_tpx3_dac_monitor_name(katherine_tpx3_dac_monitor_t monitor)

    ctypedef enum katherine_polarity_t:
        KATHERINE_POLARITY_HOLES
        KATHERINE_POLARITY_ELECTRONS

    const char *katherine_str_phase(katherine_tpx3_phase_t v)
    const char *katherine_str_freq(katherine_tpx3_freq_t v)
    const char *katherine_str_px_mode(katherine_tpx3_px_mode_t v)
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

    ctypedef enum katherine_tpx3_readout_mode_t:
        KATHERINE_TPX3_READOUT_SEQUENTIAL
        KATHERINE_TPX3_READOUT_DATA_DRIVEN

    ctypedef struct katherine_config_t:
        katherine_chip_type_t chip_type
        uint8_t chip_count

        double frame_duration_seconds
        uint32_t frame_count

        float bias[4]  # KATHERINE_CONFIG_MAX_BIAS_SUPPLIES
        katherine_trigger_t start_trigger
        katherine_trigger_t stop_trigger

    ctypedef struct katherine_tpx3_chip_config_t:
        katherine_polarity_t polarity
        bool gray_disable
        katherine_tpx3_dacs_t dacs
        katherine_tpx3_px_config_t pixels

    ctypedef struct katherine_tpx3_config_t:
        katherine_config_t common

        katherine_tpx3_readout_mode_t readout_mode
        katherine_tpx3_px_mode_t px_mode
        bool fast_vco_enable

        katherine_tpx3_phase_t clock_phase
        bool correct_clock_phase
        katherine_tpx3_freq_t clock_frequency

        katherine_tpx3_test_pulse_config_t test_pulses

        katherine_tpx3_chip_config_t chips[1]

    int katherine_config_snprint(char *buf, size_t cap, const katherine_config_t *v)
    int katherine_tpx3_chip_config_snprint(char *buf, size_t cap, const katherine_tpx3_chip_config_t *v)
    int katherine_tpx3_config_snprint(char *buf, size_t cap, const katherine_tpx3_config_t *v)

    ctypedef enum katherine_tpx3_reg_t:
        KATHERINE_TPX3_REG_TEST_PULSE_METHOD
        KATHERINE_TPX3_REG_TEST_PULSE_PERIOD
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

    int katherine_tpx3_configure(katherine_device_t *device, const katherine_tpx3_config_t *config)
    int katherine_tpx3_set_all_pixel_config(katherine_device_t *device, uint8_t chip_index, const katherine_tpx3_px_config_t *px_config)
    int katherine_set_frame_duration(katherine_device_t *device, double frame_duration_seconds)
    int katherine_tpx3_set_acq_mode(katherine_device_t *device, katherine_tpx3_px_mode_t px_mode, bool fast_vco_enabled)
    uint8_t katherine_tpx3_actual_phases(katherine_tpx3_freq_t freq, katherine_tpx3_phase_t phase)
    bint katherine_tpx3_freq_is_fast_vco_supported(katherine_tpx3_freq_t freq)
    int katherine_tpx3_dacs_validate(const katherine_tpx3_dacs_t *v)

    ctypedef struct katherine_tpx3_config_check_t:
        bint matrix_ok
        bint dacs_ok

    int katherine_tpx3_config_check_snprint(char *buf, size_t cap, const katherine_tpx3_config_check_t *v)
    int katherine_tpx3_verify_config(katherine_device_t *device, uint8_t chip_index, katherine_tpx3_config_check_t *check)
    int katherine_tpx3_set_token_count(katherine_device_t *device, uint8_t token_count)
    int katherine_set_frame_count(katherine_device_t *device, uint32_t frame_count)
    int katherine_set_bias(katherine_device_t *device, uint8_t bias_index, float bias_value)
    int katherine_tpx3_set_seq_readout_start(katherine_device_t *device, int arg)
    int katherine_set_acquisition_triggers(katherine_device_t *device, const katherine_trigger_t *start_trigger, const katherine_trigger_t *stop_trigger)
    int katherine_tpx3_set_sensor_register(katherine_device_t *device, uint8_t chip_index, katherine_tpx3_reg_t reg_idx, int32_t reg_value)
    int katherine_tpx3_update_sensor_registers(katherine_device_t *device, uint8_t chip_index)
    int katherine_tpx3_output_block_config_update(katherine_device_t *device, uint8_t chip_index)
    int katherine_tpx3_timer_set(katherine_device_t *device, uint8_t chip_index)
    int katherine_tpx3_set_dacs(katherine_device_t *device, uint8_t chip_index, const katherine_tpx3_dacs_t *dacs)
    int katherine_tpx3_set_dac(katherine_device_t *device, uint8_t chip_index, katherine_tpx3_dac_t dac, uint16_t value)
    int katherine_tpx3_set_test_pulses(katherine_device_t *device, const katherine_tpx3_test_pulse_config_t *test_pulse_config)
