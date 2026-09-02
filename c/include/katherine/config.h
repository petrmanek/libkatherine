/**
 * \file
 * \brief Functions related to detector and readout configuration.
 * \author Petr Mánek
 * \date 10.6.18
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <katherine/global.h>
#include <katherine/px_config.h>

/**
 * \addtogroup c_api
 * \{
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct katherine_device katherine_device_t;


typedef enum katherine_acquisition_mode {
    ACQUISITION_MODE_TOA_TOT    = 0,
    ACQUISITION_MODE_ONLY_TOA   = 1,
    ACQUISITION_MODE_EVENT_ITOT = 2,
} katherine_acquisition_mode_t;

KATHERINE_EXPORTED const char *
katherine_str_acquisition_mode(katherine_acquisition_mode_t mode);


typedef struct katherine_trigger {
    bool enabled;
    char channel;
    bool use_falling_edge;
} katherine_trigger_t;

KATHERINE_EXPORTED int
katherine_trigger_snprint(char *buf, size_t cap, const katherine_trigger_t *v);


typedef struct katherine_test_pulse_config {
    bool enabled;      ///< true if test pulses should be injected during acquisition
    bool digital_only; ///< false: pulse the analog frontend (amplitude = VTP_coarse - VTP_fine, DAC LSBs 5 mV / 2.5 mV), true: pulse the digital discriminator input
    bool external;     ///< set true to bypass internal pulse generator with signal from the ExtTPulse pad (in such case count/period/phase do not apply)
    uint16_t count;    ///< number of pulses per shutter opening, 1 to 65535
    uint16_t period;   ///< pulse period in pixel-clock cycles (25 ns at 40 MHz), 65 to 16321 (1.625 us to 408.025 us), rounded down to the nearest 64k + 1
    uint8_t phase;     ///< pulse edge clock phase, selects Clk_ph_shift[0..15]; keep 0 unless a multi-phase pixel clock is configured
} katherine_test_pulse_config_t;

KATHERINE_EXPORTED int
katherine_test_pulse_config_snprint(char *buf, size_t cap, const katherine_test_pulse_config_t *v);


typedef struct katherine_dacs_named {
    uint16_t Ibias_Preamp_ON;
    uint16_t Ibias_Preamp_OFF;
    uint16_t VPReamp_NCAS;
    uint16_t Ibias_Ikrum;
    uint16_t Vfbk;
    uint16_t Vthreshold_fine;
    uint16_t Vthreshold_coarse;
    uint16_t Ibias_DiscS1_ON;
    uint16_t Ibias_DiscS1_OFF;
    uint16_t Ibias_DiscS2_ON;
    uint16_t Ibias_DiscS2_OFF;
    uint16_t Ibias_PixelDAC;
    uint16_t Ibias_TPbufferIn;
    uint16_t Ibias_TPbufferOut;
    uint16_t VTP_coarse;
    uint16_t VTP_fine;
    uint16_t Ibias_CP_PLL;
    uint16_t PLL_Vcntrl;
} katherine_dacs_named_t;


typedef union katherine_dacs {
    uint16_t array[18];
    katherine_dacs_named_t named;
} katherine_dacs_t;

KATHERINE_EXPORTED int
katherine_dacs_snprint(char *buf, size_t cap, const katherine_dacs_t *v);

KATHERINE_EXPORTED int
katherine_dacs_validate(const katherine_dacs_t *v);


/// Phase distribution of the main Timepix3 clock across the pixel matrix.
/// Having more phases helps spread the load in data-intensive measurements and make the ASIC more stable.
/// Any value other than PHASE_1, however, requires a ToA correction (either by software or readout, if supported).
/// This setting is a request only: katherine_actual_phases() gives the phases actually generated.
typedef enum katherine_phase {
    PHASE_1  = 0, ///< All clocks measure with the same phase, no ToA phase correction is required.
    PHASE_2  = 1, ///< 2  clock phases, ToA phase correction is required.
    PHASE_4  = 2, ///< 4  clock phases, ToA phase correction is required.
    PHASE_8  = 3, ///< 8  clock phases, ToA phase correction is required.
    PHASE_16 = 4, ///< 16 clock phases, ToA phase correction is required.
} katherine_phase_t;


/// Frequency of the main Timepix3 clock (for ToT and ToA, but not fToA).
/// Whether fast-VCO (for fToA) may be enabled is given by katherine_freq_is_fast_vco_supported().
typedef enum katherine_freq {
    FREQ_20  = 0, ///< f =  20 MHz. Undocumented by the readout manual; corroborated by other client implementations.
    FREQ_40  = 1, ///< f =  40 MHz. Most frequently used value.
    FREQ_80  = 2, ///< f =  80 MHz.
    FREQ_160 = 3, ///< f = 160 MHz.
} katherine_freq_t;

KATHERINE_EXPORTED const char *
katherine_str_phase(katherine_phase_t phase);

KATHERINE_EXPORTED uint8_t
katherine_actual_phases(katherine_freq_t freq, katherine_phase_t phase);

KATHERINE_EXPORTED const char *
katherine_str_freq(katherine_freq_t freq);

KATHERINE_EXPORTED bool
katherine_freq_is_fast_vco_supported(katherine_freq_t freq);


typedef struct katherine_config {
    katherine_px_config_t pixel_config;

    unsigned char bias_id;

    double acq_time; // ns
    int no_frames;

    float bias;
    katherine_trigger_t start_trigger;
    bool delayed_start;
    katherine_trigger_t stop_trigger;

    bool gray_disable;
    bool polarity_holes;

    katherine_phase_t phase; ///< Phase distribution of clock signals across the ASIC. A single phase (PHASE_1) means that all timestamps are recorded in-phase. Any other setting will stagger adjacent clock signals by half of the period (PHASE_2), a quarter of the period (PHASE_4) etc., so that coincident data bursts are better distributed in time for a more stable data flow. This performance improvement however comes at the price of having to correct the phase-offsets in hit timestamps to recover true values. See the correct_phase setting below for that.
    bool correct_phase;      ///< Ask for per-double-column clock phase correction. What actually happens depends on the device and on the phase count, and is reported by katherine_acquisition_t::phase_correction once an acquisition begins.

    katherine_freq_t freq;
    katherine_dacs_t dacs;

    katherine_test_pulse_config_t test_pulse_config;
} katherine_config_t;

KATHERINE_EXPORTED int
katherine_config_snprint(char *buf, size_t cap, const katherine_config_t *v);


KATHERINE_EXPORTED int
katherine_configure(katherine_device_t *device, const katherine_config_t *config);

KATHERINE_EXPORTED int
katherine_set_all_pixel_config(katherine_device_t *device, const katherine_px_config_t *px_config);

KATHERINE_EXPORTED int
katherine_set_acq_time(katherine_device_t *device, double ns);

KATHERINE_EXPORTED int
katherine_set_acq_mode(katherine_device_t *device, katherine_acquisition_mode_t acq_mode, bool fast_vco_enabled);

KATHERINE_EXPORTED int
katherine_set_no_frames(katherine_device_t *device, int no_frames);

KATHERINE_EXPORTED int
katherine_set_bias(katherine_device_t *device, unsigned char bias_id, float bias_value);

KATHERINE_EXPORTED int
katherine_set_seq_readout_start(katherine_device_t *device, int arg);

KATHERINE_EXPORTED int
katherine_acquisition_setup(katherine_device_t *device, const katherine_trigger_t *start_trigger, bool delayed_start, const katherine_trigger_t *end_trigger);

typedef enum katherine_tpx3_reg {
    TPX3_REG_TEST_PULSE_METHOD = 0,
    // The readout firmware and Tpx3 manual Table 9 (header 0h0C) agree this
    // register carries TP_period in bits 7:0 and TP_phase in bits 11:8, not
    // a single "method" as the name above suggests. Kept as an alias rather
    // than a rename: same value, correct name.
    TPX3_REG_TEST_PULSE_PERIOD     = TPX3_REG_TEST_PULSE_METHOD,
    TPX3_REG_NUMBER_TEST_PULSES    = 1,
    TPX3_REG_OUT_BLOCK_CONFIG      = 2,
    TPX3_REG_PLL_CONFIG            = 3,
    TPX3_REG_GENERAL_CONFIG        = 4,
    TPX3_REG_SLVS_CONFIG           = 5,
    TPX3_REG_POWER_PULSING_PATTERN = 6,
    TPX3_REG_SET_TIMER_LOW         = 7,
    TPX3_REG_SET_TIMER_MID         = 8,
    TPX3_REG_SET_TIMER_HIGH        = 9,
    TPX3_REG_SENSE_DAC_SELECTOR    = 10,
    TPX3_REG_EXT_DAC_SELECTOR      = 11,
} katherine_tpx3_reg_t;

KATHERINE_EXPORTED int
katherine_set_sensor_register(katherine_device_t *device, char reg_idx, int32_t reg_value);

KATHERINE_EXPORTED int
katherine_update_sensor_registers(katherine_device_t *device);

KATHERINE_EXPORTED int
katherine_output_block_config_update(katherine_device_t *device);

KATHERINE_EXPORTED int
katherine_timer_set(katherine_device_t *device);

KATHERINE_EXPORTED int
katherine_set_dacs(katherine_device_t *device, const katherine_dacs_t *dacs);

KATHERINE_EXPORTED int
katherine_set_test_pulses(katherine_device_t *device, const katherine_test_pulse_config_t *tp_config);

#ifdef __cplusplus
}
#endif

/** \} */
