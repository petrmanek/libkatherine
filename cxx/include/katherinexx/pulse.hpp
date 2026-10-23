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

/// What one generator channel puts on its pad.
using pulse_output = katherine_pulse_output_t;
/// One generator channel: what it emits and for how long.
using pulse_channel_config = katherine_pulse_channel_config_t;
/// Every generator channel at once, plus the source that clocks them.
using pulse_config = katherine_pulse_config_t;
/// What drives the pulse generator.
using pulse_source = katherine_pulse_source_t;
/// What each generator channel has emitted, one entry per channel.
using pulse_counts = katherine_pulse_counts_t;

/** \} */

} // namespace katherine
