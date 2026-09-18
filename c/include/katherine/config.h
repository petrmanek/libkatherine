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
#include <katherine/error.h>
#include <katherine/px_config.h>

/**
 * \defgroup katherine_config Configuration
 * \ingroup katherine_c_api
 * \brief Building a configuration and applying it to a readout.
 */

/**
 * \addtogroup katherine_config
 * \{
 */

#ifdef __cplusplus
extern "C" {
#endif

#ifndef DOXYGEN_SHOULD_SKIP_THIS
// Forward declaration, to avoid a circular include with device.h. The
// definition, and its documentation, live there.
typedef struct katherine_device katherine_device_t;
#endif /* DOXYGEN_SHOULD_SKIP_THIS */


typedef enum katherine_tpx3_px_mode {
    KATHERINE_TPX3_PX_TOA_TOT          = 0,
    KATHERINE_TPX3_PX_ONLY_TOA         = 1,
    KATHERINE_TPX3_PX_EVENT_COUNT_ITOT = 2,
} katherine_tpx3_px_mode_t;

KATHERINE_EXPORTED const char *
katherine_str_px_mode(katherine_tpx3_px_mode_t mode);


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


/** Number of DACs a Timepix3 carries. */
#define KATHERINE_TPX3_DAC_COUNT 18

/**
 * Timepix3's eighteen DACs, named as its manual names them (Table 11) and
 * ordered as it codes them.
 *
 * This is where the DACs are described; everything else that names one refers
 * here. Every field is a raw DAC setting, not a physical quantity: what each
 * one spans is per DAC, and katherine_tpx3_dac_max() and
 * katherine_tpx3_dac_to_si() answer that. Ibias_* bias a current, the rest
 * set a voltage.
 *
 * These are per-chip calibration data. The manual's defaults suit no
 * particular chip, and a vector of zeros is not a neutral starting point:
 * measured on a Gen1 readout, zeroed Vfbk and PLL_Vcntrl leave every pixel
 * of the matrix firing. Copy a vector known to work on the chip at hand.
 *
 * The manual documents no function for the individual DACs -- its section
 * 4.5.1 carries Table 28 and nothing else, and 4.5.2 and 4.5.4 are empty
 * headings -- so nothing is claimed here beyond what the names and that
 * table support.
 */
typedef struct katherine_tpx3_dacs_named {
    uint16_t Ibias_Preamp_ON;   ///< Preamplifier bias while the pixel is on.
    uint16_t Ibias_Preamp_OFF;  ///< Preamplifier bias while power pulsing holds it off.
    uint16_t Vpreamp_NCAS;      ///< Preamplifier cascode voltage.
    uint16_t Ibias_Ikrum;       ///< Krummenacher feedback current, which sets the return to baseline and so the time over threshold.
    uint16_t Vfbk;              ///< Preamplifier feedback (baseline) voltage. Zero is not neutral; see above.
    uint16_t Vthreshold_fine;   ///< Discriminator threshold, fine part. With the coarse part it forms the 13-bit threshold of Table 28.
    uint16_t Vthreshold_coarse; ///< Discriminator threshold, coarse part.
    uint16_t Ibias_DiscS1_ON;   ///< First discriminator stage bias while the pixel is on.
    uint16_t Ibias_DiscS1_OFF;  ///< First discriminator stage bias while power pulsing holds it off.
    uint16_t Ibias_DiscS2_ON;   ///< Second discriminator stage bias while the pixel is on.
    uint16_t Ibias_DiscS2_OFF;  ///< Second discriminator stage bias while power pulsing holds it off.
    uint16_t Ibias_PixelDAC;    ///< Bias of the per-pixel trim DAC, which scales the four-bit local threshold of katherine_px_config_set_loc_thl().
    uint16_t Ibias_TPbufferIn;  ///< Test-pulse buffer input bias.
    uint16_t Ibias_TPbufferOut; ///< Test-pulse buffer output bias.
    uint16_t VTP_coarse;        ///< Test-pulse amplitude, coarse part. The injected amplitude is VTP_coarse - VTP_fine; see katherine_test_pulse_config_t.
    uint16_t VTP_fine;          ///< Test-pulse amplitude, fine part.
    uint16_t Ibias_CP_PLL;      ///< PLL charge-pump bias.
    uint16_t PLL_Vcntrl;        ///< PLL control voltage. Zero is not neutral; see above.
} katherine_tpx3_dacs_named_t;


typedef union katherine_tpx3_dacs {
    uint16_t array[KATHERINE_TPX3_DAC_COUNT];
    katherine_tpx3_dacs_named_t named;
} katherine_tpx3_dacs_t;

KATHERINE_EXPORTED int
katherine_tpx3_dacs_snprint(char *buf, size_t cap, const katherine_tpx3_dacs_t *v);

KATHERINE_EXPORTED katherine_error_t
katherine_tpx3_dacs_validate(const katherine_tpx3_dacs_t *v);

/**
 * Timepix3's eighteen DACs, in katherine_tpx3_dacs_named_t and
 * katherine_tpx3_dacs_t::array order. That order is the chip's own DAC Code
 * (Timepix3 manual Table 11) minus one.
 *
 * Each enumerator copies the description of the field it indexes, so the
 * DACs are described in one place -- katherine_tpx3_dacs_named_t -- and here only
 * referred to.
 */
typedef enum katherine_tpx3_dac {
    KATHERINE_TPX3_DAC_IBIAS_PREAMP_ON = 0, ///< \copydoc katherine_tpx3_dacs_named_t::Ibias_Preamp_ON
    KATHERINE_TPX3_DAC_IBIAS_PREAMP_OFF,    ///< \copydoc katherine_tpx3_dacs_named_t::Ibias_Preamp_OFF
    KATHERINE_TPX3_DAC_VPREAMP_NCAS,        ///< \copydoc katherine_tpx3_dacs_named_t::Vpreamp_NCAS
    KATHERINE_TPX3_DAC_IBIAS_IKRUM,         ///< \copydoc katherine_tpx3_dacs_named_t::Ibias_Ikrum
    KATHERINE_TPX3_DAC_VFBK,                ///< \copydoc katherine_tpx3_dacs_named_t::Vfbk
    KATHERINE_TPX3_DAC_VTHRESHOLD_FINE,     ///< \copydoc katherine_tpx3_dacs_named_t::Vthreshold_fine
    KATHERINE_TPX3_DAC_VTHRESHOLD_COARSE,   ///< \copydoc katherine_tpx3_dacs_named_t::Vthreshold_coarse
    KATHERINE_TPX3_DAC_IBIAS_DISCS1_ON,     ///< \copydoc katherine_tpx3_dacs_named_t::Ibias_DiscS1_ON
    KATHERINE_TPX3_DAC_IBIAS_DISCS1_OFF,    ///< \copydoc katherine_tpx3_dacs_named_t::Ibias_DiscS1_OFF
    KATHERINE_TPX3_DAC_IBIAS_DISCS2_ON,     ///< \copydoc katherine_tpx3_dacs_named_t::Ibias_DiscS2_ON
    KATHERINE_TPX3_DAC_IBIAS_DISCS2_OFF,    ///< \copydoc katherine_tpx3_dacs_named_t::Ibias_DiscS2_OFF
    KATHERINE_TPX3_DAC_IBIAS_PIXELDAC,      ///< \copydoc katherine_tpx3_dacs_named_t::Ibias_PixelDAC
    KATHERINE_TPX3_DAC_IBIAS_TPBUFFERIN,    ///< \copydoc katherine_tpx3_dacs_named_t::Ibias_TPbufferIn
    KATHERINE_TPX3_DAC_IBIAS_TPBUFFEROUT,   ///< \copydoc katherine_tpx3_dacs_named_t::Ibias_TPbufferOut
    KATHERINE_TPX3_DAC_VTP_COARSE,          ///< \copydoc katherine_tpx3_dacs_named_t::VTP_coarse
    KATHERINE_TPX3_DAC_VTP_FINE,            ///< \copydoc katherine_tpx3_dacs_named_t::VTP_fine
    KATHERINE_TPX3_DAC_IBIAS_CP_PLL,        ///< \copydoc katherine_tpx3_dacs_named_t::Ibias_CP_PLL
    KATHERINE_TPX3_DAC_PLL_VCNTRL,          ///< \copydoc katherine_tpx3_dacs_named_t::PLL_Vcntrl
} katherine_tpx3_dac_t;

/**
 * The physical quantity a DAC sets, and so the unit
 * katherine_tpx3_dac_to_si() reports it in.
 *
 * Not namespaced, unlike the DACs themselves: which DACs exist and how wide
 * they are is Timepix3's, but a DAC either biases a current or sets a
 * voltage on any ASIC, so a second one would use this unchanged.
 */
typedef enum katherine_dac_unit {
    KATHERINE_DAC_UNIT_AMP = 0, ///< Amperes; the DAC biases a current.
    KATHERINE_DAC_UNIT_VOLT,    ///< Volts; the DAC sets a voltage.
} katherine_dac_unit_t;

KATHERINE_EXPORTED const char *
katherine_tpx3_dac_name(katherine_tpx3_dac_t dac);

KATHERINE_EXPORTED uint16_t
katherine_tpx3_dac_max(katherine_tpx3_dac_t dac);

KATHERINE_EXPORTED double
katherine_tpx3_dac_to_si(katherine_tpx3_dac_t dac, uint16_t value, katherine_dac_unit_t *unit);


/**
 * Phase distribution of the main Timepix3 clock across the pixel matrix.
 * Having more phases helps spread the load in data-intensive measurements
 * and make the ASIC more stable. Any value other than
 * KATHERINE_TPX3_PHASE_1, however, requires a ToA correction (either by
 * software or readout, if supported). This setting is a request only:
 * katherine_actual_phases() gives the phases actually generated.
 */
typedef enum katherine_tpx3_phase {
    KATHERINE_TPX3_PHASE_1  = 0, ///< All clocks measure with the same phase, no ToA phase correction is required.
    KATHERINE_TPX3_PHASE_2  = 1, ///< 2  clock phases, ToA phase correction is required.
    KATHERINE_TPX3_PHASE_4  = 2, ///< 4  clock phases, ToA phase correction is required.
    KATHERINE_TPX3_PHASE_8  = 3, ///< 8  clock phases, ToA phase correction is required.
    KATHERINE_TPX3_PHASE_16 = 4, ///< 16 clock phases, ToA phase correction is required.
} katherine_tpx3_phase_t;


/**
 * Frequency of the main Timepix3 clock (for ToT and ToA, but not fToA).
 * Whether fast-VCO (for fToA) may be enabled is given by katherine_freq_is_fast_vco_supported().
 */
typedef enum katherine_tpx3_freq {
    KATHERINE_TPX3_FREQ_20_MHZ  = 0, ///< f =  20 MHz. Undocumented by the readout manual; corroborated by other client implementations.
    KATHERINE_TPX3_FREQ_40_MHZ  = 1, ///< f =  40 MHz. Most frequently used value.
    KATHERINE_TPX3_FREQ_80_MHZ  = 2, ///< f =  80 MHz.
    KATHERINE_TPX3_FREQ_160_MHZ = 3, ///< f = 160 MHz.
} katherine_tpx3_freq_t;

KATHERINE_EXPORTED const char *
katherine_str_phase(katherine_tpx3_phase_t phase);

KATHERINE_EXPORTED uint8_t
katherine_actual_phases(katherine_tpx3_freq_t freq, katherine_tpx3_phase_t phase);

KATHERINE_EXPORTED const char *
katherine_str_freq(katherine_tpx3_freq_t freq);

KATHERINE_EXPORTED bool
katherine_freq_is_fast_vco_supported(katherine_tpx3_freq_t freq);


/**
 * Charge carriers the sensor collects, and therefore the bias polarity the
 * assembly is operated at.
 *
 * Values are the Polarity[0] bit of GeneralConfig itself (Timepix3 manual
 * Table 18: 0 collects holes, 1 collects electrons), so the setting can be
 * compared against a register read-back without translation.
 *
 * Hole collection is zero deliberately. A configuration is often zeroed
 * before its fields are filled, and the wrong polarity destroys the chip, so
 * the value a forgotten field takes must be the conservative one.
 */
typedef enum katherine_polarity {
    KATHERINE_POLARITY_HOLES     = 0, ///< Collect holes (h+).
    KATHERINE_POLARITY_ELECTRONS = 1, ///< Collect electrons (e-).
} katherine_polarity_t;

/**
 * Get stable, lowercase description of a carrier polarity.
 * \param polarity Polarity to describe
 * \return Null-terminated string. "unknown" for a value outside the enum.
 */
KATHERINE_EXPORTED const char *
katherine_str_polarity(katherine_polarity_t polarity);

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

    /**
     * Carriers the sensor collects. Zero is KATHERINE_POLARITY_HOLES, so a
     * configuration whose fields are not all filled in selects the polarity
     * that cannot destroy the chip.
     */
    katherine_polarity_t polarity;

    /**
     * Phase distribution of clock signals across the ASIC. A single phase
     * (KATHERINE_TPX3_PHASE_1) means that all timestamps are recorded
     * in-phase. Any other setting will stagger adjacent clock signals by
     * half of the period (KATHERINE_TPX3_PHASE_2), a quarter of the period
     * (KATHERINE_TPX3_PHASE_4) etc., so that coincident data bursts are
     * better distributed in time for a more stable data flow. This
     * performance improvement however comes at the price of having to
     * correct the phase-offsets in hit timestamps to recover true values.
     * See the correct_phase setting below for that.
     */
    katherine_tpx3_phase_t phase;
    bool correct_phase; ///< Ask for per-double-column clock phase correction. What actually happens depends on the device and on the phase count, and is reported by katherine_acquisition_t::phase_correction once an acquisition begins.

    katherine_tpx3_freq_t freq;
    katherine_tpx3_dacs_t dacs;

    katherine_test_pulse_config_t test_pulse_config;
} katherine_config_t;

KATHERINE_EXPORTED int
katherine_config_snprint(char *buf, size_t cap, const katherine_config_t *v);


KATHERINE_EXPORTED katherine_error_t
katherine_configure(katherine_device_t *device, const katherine_config_t *config);

KATHERINE_EXPORTED katherine_error_t
katherine_set_all_pixel_config(katherine_device_t *device, const katherine_px_config_t *px_config);

KATHERINE_EXPORTED katherine_error_t
katherine_set_acq_time(katherine_device_t *device, double ns);

KATHERINE_EXPORTED katherine_error_t
katherine_set_acq_mode(katherine_device_t *device, katherine_tpx3_px_mode_t px_mode, bool fast_vco_enabled);

KATHERINE_EXPORTED katherine_error_t
katherine_set_no_frames(katherine_device_t *device, int no_frames);

KATHERINE_EXPORTED katherine_error_t
katherine_tpx3_set_token_count(katherine_device_t *device, uint8_t token_count);

KATHERINE_EXPORTED katherine_error_t
katherine_set_bias(katherine_device_t *device, unsigned char bias_id, float bias_value);

KATHERINE_EXPORTED katherine_error_t
katherine_set_seq_readout_start(katherine_device_t *device, int arg);

// Declared here, with the rest of the configuration calls, but documented
// with the acquisition it sets up. The enclosing group has to be closed and
// reopened around it: \ingroup on the declaration would add a second
// membership rather than replace the first, and the function would then
// appear on both pages.
/** \} */

/**
 * \addtogroup katherine_acquisition
 * \{
 */

KATHERINE_EXPORTED katherine_error_t
katherine_acquisition_setup(katherine_device_t *device, const katherine_trigger_t *start_trigger, bool delayed_start, const katherine_trigger_t *end_trigger);

/** \} */

/**
 * \addtogroup katherine_config
 * \{
 */

typedef enum katherine_tpx3_reg {
    KATHERINE_TPX3_REG_TEST_PULSE_METHOD = 0,
    /**
     * The readout firmware and Tpx3 manual Table 9 (header 0h0C) agree this
     * register carries TP_period in bits 7:0 and TP_phase in bits 11:8, not
     * a single "method" as the name above suggests. Kept as an alias rather
     * than a rename: same value, correct name.
     */
    KATHERINE_TPX3_REG_TEST_PULSE_PERIOD     = KATHERINE_TPX3_REG_TEST_PULSE_METHOD,
    KATHERINE_TPX3_REG_NUMBER_TEST_PULSES    = 1,
    KATHERINE_TPX3_REG_OUT_BLOCK_CONFIG      = 2,
    KATHERINE_TPX3_REG_PLL_CONFIG            = 3,
    KATHERINE_TPX3_REG_GENERAL_CONFIG        = 4,
    KATHERINE_TPX3_REG_SLVS_CONFIG           = 5,
    KATHERINE_TPX3_REG_POWER_PULSING_PATTERN = 6,
    KATHERINE_TPX3_REG_SET_TIMER_LOW         = 7,
    KATHERINE_TPX3_REG_SET_TIMER_MID         = 8,
    KATHERINE_TPX3_REG_SET_TIMER_HIGH        = 9,
    KATHERINE_TPX3_REG_SENSE_DAC_SELECTOR    = 10,
    KATHERINE_TPX3_REG_EXT_DAC_SELECTOR      = 11,
} katherine_tpx3_reg_t;

KATHERINE_EXPORTED katherine_error_t
katherine_set_sensor_register(katherine_device_t *device, char reg_idx, int32_t reg_value);

KATHERINE_EXPORTED katherine_error_t
katherine_update_sensor_registers(katherine_device_t *device);

KATHERINE_EXPORTED katherine_error_t
katherine_output_block_config_update(katherine_device_t *device);

KATHERINE_EXPORTED katherine_error_t
katherine_timer_set(katherine_device_t *device);

KATHERINE_EXPORTED katherine_error_t
katherine_set_dacs(katherine_device_t *device, const katherine_tpx3_dacs_t *dacs);

KATHERINE_EXPORTED katherine_error_t
katherine_set_test_pulses(katherine_device_t *device, const katherine_test_pulse_config_t *tp_config);

#ifdef __cplusplus
}
#endif

/** \} */
