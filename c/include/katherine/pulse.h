/**
 * \file
 * \brief The readout's pulse generator.
 * \author Petr Mánek
 * \date 18.9.26
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

/**
 * \defgroup katherine_pulse Pulse generator
 * \ingroup katherine_c_api
 * \brief Producing pulses from a trigger, or from the readout's own
 *   oscillator.
 */

/**
 * \addtogroup katherine_pulse
 * \{
 */

#ifdef __cplusplus
extern "C" {
#endif

#ifndef DOXYGEN_SHOULD_SKIP_THIS
typedef struct katherine_device katherine_device_t;
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

/**
 * The largest possible number of pulse channels on any supported readout
 * device, useful for allocating arrays that list them all.
 *
 * It is not necessarily equal to the actual number of pulse channels on a
 * specific device. That quantity generally varies from model to model, and
 * is reported in \ref katherine_device_derived_info_t::pulse_channel_count.
 * In an array, all items past that number are disregarded.
 */
#define KATHERINE_MAX_PULSE_CHANNEL_COUNT 4

/** Output signal of a pulse channel. */
typedef enum katherine_pulse_output {
    KATHERINE_PULSE_OUTPUT_PULSE = 0,            ///< The channel's own pulse.
    KATHERINE_PULSE_OUTPUT_PULSE_INVERTED,       ///< That pulse, inverted.
    KATHERINE_PULSE_OUTPUT_PASSTHROUGH,          ///< The trigger, carried through untouched.
    KATHERINE_PULSE_OUTPUT_PASSTHROUGH_INVERTED, ///< That trigger, inverted.
} katherine_pulse_output_t;

/**
 * Source of the generator's trigger.
 *
 * A rising edge is zero, so that a zeroed configuration keeps the behaviour
 * the two flags this replaces had when they were false.
 */
typedef enum katherine_pulse_source {
    KATHERINE_PULSE_SOURCE_RISING_EDGE = 0, ///< Rising edge of the input trigger.
    KATHERINE_PULSE_SOURCE_FALLING_EDGE,    ///< Falling edge of the input trigger.
    KATHERINE_PULSE_SOURCE_OSCILLATOR,      ///< The readout's own oscillator, standing in for an input.
} katherine_pulse_source_t;

/** Configuration of a single pulse channel. */
typedef struct katherine_pulse_channel_config {
    bool enabled;                    ///< True to drive the channel's own output.
    katherine_pulse_output_t output; ///< Output signal of the channel.
    double length_seconds;           ///< Duration of its pulse, honoured to 10 ns or better.
    double delay_seconds;            ///< Interval between the trigger and the pulse, honoured to 10 ns or better. At most 0.167 s on this readout; a longer one is refused.
} katherine_pulse_channel_config_t;

/** Configuration of all pulse channels in a readout. */
typedef struct katherine_pulse_config {
    katherine_pulse_channel_config_t channel[KATHERINE_MAX_PULSE_CHANNEL_COUNT]; ///< One entry per channel, of which the readout uses its own katherine_device_derived_info_t::pulse_channel_count. The rest are disregarded.

    bool only_when_ready;            ///< True to hold pulses back until the readout reports itself ready.
    katherine_pulse_source_t source; ///< Source of the generator's trigger.
    double oscillator_frequency_hz;  ///< Frequency of the simulated trigger, honoured to within one part in the readout's own 50 MHz. Disregarded unless source is KATHERINE_PULSE_SOURCE_OSCILLATOR.
} katherine_pulse_config_t;

/** The generator's measurements of the trigger reaching it. */
typedef struct katherine_pulse_counts {
    double low_seconds;    ///< Duration of the trigger's last low interval.
    double high_seconds;   ///< Duration of its last high interval.
    uint32_t rising_edges; ///< Rising edges counted since the last reset.
    uint32_t clock_mhz;    ///< Frequency of the generator's clock, in megahertz, as the readout reports it.
} katherine_pulse_counts_t;

KATHERINE_EXPORTED katherine_error_t
katherine_set_pulse_generator(katherine_device_t *device, const katherine_pulse_config_t *config);

KATHERINE_EXPORTED katherine_error_t
katherine_get_pulse_generator_counts(katherine_device_t *device, katherine_pulse_counts_t *counts);

KATHERINE_EXPORTED katherine_error_t
katherine_reset_pulse_generator_counts(katherine_device_t *device);

KATHERINE_EXPORTED const char *
katherine_str_pulse_output(katherine_pulse_output_t output);

KATHERINE_EXPORTED const char *
katherine_str_pulse_source(katherine_pulse_source_t source);

#ifdef __cplusplus
}
#endif

/** \} */
