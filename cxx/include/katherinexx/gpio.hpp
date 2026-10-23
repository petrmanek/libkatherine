/**
 * \file
 * \brief C++ wrapper for the readout's GPIO channels.
 * \author Petr Mánek
 * \date 18.9.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <katherine/gpio.h>

namespace katherine {

/**
 * \addtogroup katherine_cxx_api
 * \{
 */

using gpio_mapping         = katherine_gpio_mapping_t;
using gpio_ready           = katherine_gpio_ready_t;
using gpio_monitor_signal  = katherine_gpio_monitor_signal_t;
using gpio_channel_config  = katherine_gpio_channel_config_t;
using gpio_config          = katherine_gpio_config_t;
using gpio_channel_monitor = katherine_gpio_channel_monitor_t;
using gpio_monitor         = katherine_gpio_monitor_t;

/**
 * The frequency a probed channel's tick counts work out to, in Hertz.
 *
 * The readout counts ticks rather than measuring a frequency, so this is
 * arithmetic over what katherine::device::probe_gpio() brought back, not a
 * second question for the readout. Zero where the channel saw no edges.
 */
inline double
gpio_channel_frequency(const gpio_channel_monitor& v)
{
    return katherine_gpio_channel_frequency(&v);
}

/// The fraction of each period a probed channel spent high, from 0 to 1.
inline double
gpio_channel_duty_cycle(const gpio_channel_monitor& v)
{
    return katherine_gpio_channel_duty_cycle(&v);
}

/** \} */

} // namespace katherine
