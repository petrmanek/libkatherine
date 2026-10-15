/**
 * \file
 * \brief General-purpose input and output channels of a readout.
 *
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
 * \defgroup katherine_gpio General-purpose inputs and outputs
 * \ingroup katherine_c_api
 * \brief Setting up the readout's input and output channels, and reading what
 *   they carry.
 */

/**
 * \addtogroup katherine_gpio
 * \{
 */

#ifdef __cplusplus
extern "C" {
#endif

#ifndef DOXYGEN_SHOULD_SKIP_THIS
typedef struct katherine_device katherine_device_t;
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

/**
 * The largest possible number of GPIO channels on any supported readout
 * device, useful for allocating arrays that list them all.
 *
 * It is not necessarily equal to the actual number of GPIO channels on a
 * specific device. That quantity generally varies from model to model, and
 * is reported in \ref katherine_device_derived_info_t::all_gpio_count. In an
 * array, all items past that number are disregarded.
 */
#define KATHERINE_MAX_GPIO_COUNT 8

/** Period of the counters katherine_gpio_channel_monitor_t reports, in nanoseconds. */
#define KATHERINE_GPIO_TICK_NS   10

/**
 * Programmed function of a single GPIO channel. Whether it acts as an input
 * or output, and in the latter case, what signal it carries.
 *
 * The readout multiplexes one of these onto each output channel. Everything
 * from KATHERINE_GPIO_SPARE_0 up is reserved by the firmware and included
 * only so that a value read back from a readout can be named.
 */
typedef enum katherine_gpio_mapping {
    KATHERINE_GPIO_INPUT = 0, ///< An input, whose logic level katherine_probe_gpio() reports.

    KATHERINE_GPIO_LOW, ///< Constant logic low.

    KATHERINE_GPIO_PULSE_INTERNAL,  ///< Internal channel of the trigger generator.
    KATHERINE_GPIO_PULSE_CHANNEL_0, ///< Output channel 0 of the trigger generator. Not to be confused with GPIO channel 0, this is a programmable digital function generator that can be driven by other input signals.
    KATHERINE_GPIO_PULSE_CHANNEL_1, ///< Output channel 1 of the trigger generator. Not to be confused with GPIO channel 1, this is a programmable digital function generator that can be driven by other input signals.
    KATHERINE_GPIO_PULSE_CHANNEL_2, ///< Output channel 2 of the trigger generator. Not to be confused with GPIO channel 2, this is a programmable digital function generator that can be driven by other input signals.

    KATHERINE_GPIO_LEVEL_0, ///< Logic level of channel 0, repeated onto this channel.
    KATHERINE_GPIO_LEVEL_1, ///< Logic level of channel 1, repeated onto this channel.
    KATHERINE_GPIO_LEVEL_2, ///< Logic level of channel 2, repeated onto this channel.
    KATHERINE_GPIO_LEVEL_3, ///< Logic level of channel 3, repeated onto this channel.
    KATHERINE_GPIO_LEVEL_4, ///< Logic level of channel 4, repeated onto this channel.
    KATHERINE_GPIO_LEVEL_5, ///< Logic level of channel 5, repeated onto this channel.
    KATHERINE_GPIO_LEVEL_6, ///< Logic level of channel 6, repeated onto this channel.
    KATHERINE_GPIO_LEVEL_7, ///< Logic level of channel 7, repeated onto this channel.

    KATHERINE_GPIO_READY_LOCAL,    ///< High when all locally connected chips are ready to take data.
    KATHERINE_GPIO_READY_COMPOSED, ///< High when all locally connected chips are ready to take data, composed together with GPIO inputs. \see katherine_gpio_channel_config_t::ready

    KATHERINE_GPIO_EVENT_TRIGGER, ///< Reserved for on-readout event analysis.

    KATHERINE_GPIO_CHIP_0_PIXEL_DATA, ///< High while chip 0 is reporting hits.
    KATHERINE_GPIO_CHIP_1_PIXEL_DATA, ///< High while chip 1 is reporting hits.
    KATHERINE_GPIO_CHIP_2_PIXEL_DATA, ///< High while chip 2 is reporting hits.
    KATHERINE_GPIO_CHIP_3_PIXEL_DATA, ///< High while chip 3 is reporting hits.

    KATHERINE_GPIO_SHUTTER, ///< The chip's shutter signal: high while it is sensitive.

    KATHERINE_GPIO_CASCADE_CHAIN, ///< Daisy chaining: a trigger first, then a ready signal.

    KATHERINE_GPIO_CLOCK,       ///< The readout's own clock, usually 40 MHz.
    KATHERINE_GPIO_SYNC_SIGNAL, ///< Feedback from chip 0.

    KATHERINE_GPIO_SPARE_0, ///< Reserved by the firmware.
    KATHERINE_GPIO_SPARE_1, ///< \copydoc KATHERINE_GPIO_SPARE_0
    KATHERINE_GPIO_SPARE_2, ///< \copydoc KATHERINE_GPIO_SPARE_0
    KATHERINE_GPIO_SPARE_3, ///< \copydoc KATHERINE_GPIO_SPARE_0
    KATHERINE_GPIO_SPARE_4, ///< \copydoc KATHERINE_GPIO_SPARE_0
    KATHERINE_GPIO_SPARE_5, ///< \copydoc KATHERINE_GPIO_SPARE_0
    KATHERINE_GPIO_SPARE_6, ///< \copydoc KATHERINE_GPIO_SPARE_0

    KATHERINE_GPIO_HIGH, ///< Constant logic high.
} katherine_gpio_mapping_t;

/** How a GPIO channel contributes to the composed ready signal. \see KATHERINE_GPIO_READY_COMPOSED */
typedef enum katherine_gpio_ready {
    KATHERINE_GPIO_READY_UNUSED = 0, ///< The composed ready signal does not depend on the level of this GPIO channel.
    KATHERINE_GPIO_READY_WHEN_HIGH,  ///< The composed ready signal is high only while this GPIO channel is high.
    KATHERINE_GPIO_READY_WHEN_LOW,   ///< The composed ready signal is high only while this GPIO channel is low.
} katherine_gpio_ready_t;

/** Configuration of a single GPIO channel. */
typedef struct katherine_gpio_channel_config {
    katherine_gpio_mapping_t mapping; ///< Programmed function of the channel, which determines whether it acts as an input or output, and in the latter case, what signal it carries. Be advised that depending on device model some channels can only support input mode, or only support output mode. This is reported in device documentation.
    bool termination;                 ///< True to terminate the line with the readout's internal pull-down resistor, which is meaningful for an input. Only the channels reaching a connector can be terminated, see katherine_device_derived_info_t::accessible_gpio_count; katherine_set_gpio_config() refuses the others.
    katherine_gpio_ready_t ready;     ///< Whether and how the channel contributes to the composed ready signal.
} katherine_gpio_channel_config_t;

/** Configuration of all GPIO channels in a readout. */
typedef struct katherine_gpio_config {
    katherine_gpio_channel_config_t channel[KATHERINE_MAX_GPIO_COUNT]; ///< One entry per channel, of which the readout uses its own katherine_device_derived_info_t::all_gpio_count. The rest are disregarded.
} katherine_gpio_config_t;

/** Automated classification of the signal a monitored GPIO channel carries. */
typedef enum katherine_gpio_monitor_signal {
    KATHERINE_GPIO_MONITOR_SIGNAL_UNKNOWN = 0, ///< The readout does not time this channel, so its level was sampled without a classification. Which channels a readout times depends on the model.
    KATHERINE_GPIO_MONITOR_SIGNAL_CONSTANT,    ///< The signal appears constant. No rising/falling edge registered within the readout's measuring window (~5 seconds).
    KATHERINE_GPIO_MONITOR_SIGNAL_PERIODIC,    ///< Rising and falling edges were registered, so the tick counts below are meaningful.
} katherine_gpio_monitor_signal_t;

/** Measured state of a single GPIO channel. */
typedef struct katherine_gpio_channel_monitor {
    bool level;                             ///< Logic level sampled at the exact moment of the probe. True for logic high, false for logic low.
    katherine_gpio_monitor_signal_t signal; ///< Readout's classification of the signal patterns.
    uint32_t high_ticks;                    ///< The number of ticks the channel last spent high. A single tick corresponds to \ref KATHERINE_GPIO_TICK_NS. Can be used to determine signal duty cycle. Only meaningful if signal == KATHERINE_GPIO_MONITOR_SIGNAL_PERIODIC.
    uint32_t low_ticks;                     ///< The number of ticks the channel last spent low. A single tick corresponds to \ref KATHERINE_GPIO_TICK_NS. Can be used to determine signal duty cycle. Only meaningful if signal == KATHERINE_GPIO_MONITOR_SIGNAL_PERIODIC.
} katherine_gpio_channel_monitor_t;

/** Measured state of all GPIO channels in a readout. */
typedef struct katherine_gpio_monitor {
    katherine_gpio_channel_monitor_t channel[KATHERINE_MAX_GPIO_COUNT]; ///< One entry per channel, of which the readout fills its own katherine_device_derived_info_t::all_gpio_count. The rest carry nothing.
} katherine_gpio_monitor_t;

KATHERINE_EXPORTED katherine_error_t
katherine_set_gpio_config(katherine_device_t *device, const katherine_gpio_config_t *config);

KATHERINE_EXPORTED katherine_error_t
katherine_set_composed_ready_signal(katherine_device_t *device, const katherine_gpio_config_t *config);

KATHERINE_EXPORTED katherine_error_t
katherine_probe_gpio(katherine_device_t *device, katherine_gpio_monitor_t *state);

KATHERINE_EXPORTED const char *
katherine_str_gpio_mapping(katherine_gpio_mapping_t mapping);

KATHERINE_EXPORTED const char *
katherine_str_gpio_monitor_signal(katherine_gpio_monitor_signal_t signal);

KATHERINE_EXPORTED double
katherine_gpio_channel_frequency(const katherine_gpio_channel_monitor_t *v);

KATHERINE_EXPORTED double
katherine_gpio_channel_duty_cycle(const katherine_gpio_channel_monitor_t *v);

KATHERINE_EXPORTED int
katherine_gpio_channel_monitor_snprint(char *buf, size_t cap, const katherine_gpio_channel_monitor_t *v);

KATHERINE_EXPORTED int
katherine_gpio_monitor_snprint(char *buf, size_t cap, const katherine_gpio_monitor_t *v);

#ifdef __cplusplus
}
#endif

/** \} */
