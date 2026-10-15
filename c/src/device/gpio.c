/**
 * \file
 * \brief Naming and rendering of the GPIO channels.
 * \author Petr Mánek
 * \date 18.9.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <katherine/gpio.h>

/**
 * Names of the mappings, in enumeration order. Their meanings are on the
 * enumerators; this is only the spelling.
 */
static const char *const KATHERINE_GPIO_MAPPING_NAME[] = {
    // clang-format off
    "input",
    "low",
    "pulse_internal",
    "pulse_channel_0",
    "pulse_channel_1",
    "pulse_channel_2",
    "level_0",
    "level_1",
    "level_2",
    "level_3",
    "level_4",
    "level_5",
    "level_6",
    "level_7",
    "ready_local",
    "ready_composed",
    "event_trigger",
    "chip_0_pixel_data",
    "chip_1_pixel_data",
    "chip_2_pixel_data",
    "chip_3_pixel_data",
    "shutter",
    "cascade_chain",
    "clock",
    "sync_signal",
    "spare_0",
    "spare_1",
    "spare_2",
    "spare_3",
    "spare_4",
    "spare_5",
    "spare_6",
    "high",
    // clang-format on
};

/**
 * Name of a channel mapping.
 * \param mapping Mapping to name
 * \return Its name, or "unknown" outside the enumeration.
 */
const char *
katherine_str_gpio_mapping(katherine_gpio_mapping_t mapping)
{
    const size_t n = sizeof(KATHERINE_GPIO_MAPPING_NAME) / sizeof(KATHERINE_GPIO_MAPPING_NAME[0]);

    if ((unsigned) mapping >= n) return "unknown";

    return KATHERINE_GPIO_MAPPING_NAME[mapping];
}

/**
 * Name of what a channel's level is doing.
 * \param signal Signal state to name
 * \return Its name, or "unknown" outside the enumeration.
 */
const char *
katherine_str_gpio_monitor_signal(katherine_gpio_monitor_signal_t signal)
{
    switch (signal) {
    case KATHERINE_GPIO_MONITOR_SIGNAL_UNKNOWN:  return "unknown";
    case KATHERINE_GPIO_MONITOR_SIGNAL_CONSTANT: return "constant";
    case KATHERINE_GPIO_MONITOR_SIGNAL_PERIODIC: return "periodic";
    }

    return "unknown";
}

/**
 * Frequency of a channel's signal.
 * \param v Channel state, as katherine_probe_gpio() filled it
 * \return The frequency in Hertz, or zero where the readout measured no
 *   period -- a constant level, or one of the channels it does not time.
 */
double
katherine_gpio_channel_frequency(const katherine_gpio_channel_monitor_t *v)
{
    const uint64_t period = (uint64_t) v->high_ticks + v->low_ticks;

    if (v->signal != KATHERINE_GPIO_MONITOR_SIGNAL_PERIODIC || period == 0) return 0.0;

    return 1e9 / ((double) period * KATHERINE_GPIO_TICK_NS);
}

/**
 * Fraction of a channel's period spent high.
 * \param v Channel state, as katherine_probe_gpio() filled it
 * \return The fraction, between 0 and 1, or zero where the readout measured
 *   no period.
 */
double
katherine_gpio_channel_duty_cycle(const katherine_gpio_channel_monitor_t *v)
{
    const uint64_t period = (uint64_t) v->high_ticks + v->low_ticks;

    if (v->signal != KATHERINE_GPIO_MONITOR_SIGNAL_PERIODIC || period == 0) return 0.0;

    return (double) v->high_ticks / (double) period;
}
