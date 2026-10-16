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

/** \} */

} // namespace katherine
