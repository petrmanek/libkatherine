/**
 * \file
 * \brief C++ wrapper for the readout's pulse generator.
 * \author Petr Mánek
 * \date 18.9.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <katherine/pulse.h>

namespace katherine {

/**
 * \addtogroup katherine_cxx_api
 * \{
 */

using pulse_output         = katherine_pulse_output_t;
using pulse_channel_config = katherine_pulse_channel_config_t;
using pulse_config         = katherine_pulse_config_t;
using pulse_source         = katherine_pulse_source_t;
using pulse_counts         = katherine_pulse_counts_t;

/** \} */

} // namespace katherine
