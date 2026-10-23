/**
 * \file
 * \brief C++ wrapper for the readout's timing unit.
 * \author Petr Mánek
 * \date 18.9.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <cstdint>

#include <katherine/tdc.h>

namespace katherine {

/**
 * \addtogroup katherine_cxx_api
 * \{
 */

/// What one channel of the readout's timing unit watches.
using tdc_source = katherine_tdc_source_t;
/// One timing-unit channel: what it watches, and whether it is watching.
using tdc_channel_config = katherine_tdc_channel_config_t;
/// Every timing-unit channel at once, as katherine::device::set_tdc_config() takes it.
using tdc_config = katherine_tdc_config_t;

/// What each timing-unit channel has counted, one entry per channel.
using tdc_counts = std::array<uint32_t, KATHERINE_MAX_TDC_CHANNEL_COUNT>;

/** \} */

} // namespace katherine
