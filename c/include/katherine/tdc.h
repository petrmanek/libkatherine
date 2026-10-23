/**
 * \file
 * \brief The readout's timing unit.
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
#include <katherine/config.h>

/**
 * \defgroup katherine_tdc Timing unit
 * \ingroup katherine_c_api
 * \brief Timestamping signals alongside a measurement, and counting the
 *   timestamps.
 */

/**
 * \addtogroup katherine_tdc
 * \{
 */

#ifdef __cplusplus
extern "C" {
#endif

#ifndef DOXYGEN_SHOULD_SKIP_THIS
typedef struct katherine_device katherine_device_t;
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

/**
 * The largest possible number of timing channels on any supported readout
 * device, useful for allocating arrays that list them all.
 *
 * It is not necessarily equal to the actual number of timing channels on a
 * specific device. That quantity generally varies from model to model, and
 * is reported in \ref katherine_device_derived_info_t::tdc_channel_count. In
 * an array, all items past that number are disregarded.
 */
#define KATHERINE_MAX_TDC_CHANNEL_COUNT 5

/** Signal source for timestamping. */
typedef enum katherine_tdc_source {
    KATHERINE_TDC_SOURCE_GPIO_0 = 0, ///< Level of GPIO channel 0.
    KATHERINE_TDC_SOURCE_GPIO_1,     ///< Level of GPIO channel 1.
    KATHERINE_TDC_SOURCE_GPIO_2,     ///< Level of GPIO channel 2.
    KATHERINE_TDC_SOURCE_GPIO_3,     ///< Level of GPIO channel 3.

    KATHERINE_TDC_SOURCE_CHIP_0_FEEDBACK, ///< Feedback from chip 0.
    KATHERINE_TDC_SOURCE_CHIP_1_FEEDBACK, ///< Feedback from chip 1.
    KATHERINE_TDC_SOURCE_CHIP_2_FEEDBACK, ///< Feedback from chip 2.
    KATHERINE_TDC_SOURCE_CHIP_3_FEEDBACK, ///< Feedback from chip 3.
} katherine_tdc_source_t;

/** Configuration of a single timing channel. */
typedef struct katherine_tdc_channel_config {
    bool enabled;                  ///< True to timestamp edges on this channel.
    katherine_tdc_source_t source; ///< Signal source for timestamping. Not transmitted on the first generation, whose firmware has no field for it.
    katherine_trigger_edge_t edge; ///< Rising or falling edge of the source.
} katherine_tdc_channel_config_t;

/** Configuration of all timing channels in a readout. */
typedef struct katherine_tdc_config {
    katherine_tdc_channel_config_t channel[KATHERINE_MAX_TDC_CHANNEL_COUNT]; ///< One entry per channel, of which the readout uses its own katherine_device_derived_info_t::tdc_channel_count. The rest are disregarded.
} katherine_tdc_config_t;

KATHERINE_EXPORTED katherine_error_t
katherine_set_tdc_config(katherine_device_t *device, const katherine_tdc_config_t *config);

KATHERINE_EXPORTED katherine_error_t
katherine_get_tdc_counts(katherine_device_t *device, uint32_t counts[KATHERINE_MAX_TDC_CHANNEL_COUNT]);

KATHERINE_EXPORTED const char *
katherine_str_tdc_source(katherine_tdc_source_t source);

#ifdef __cplusplus
}
#endif

/** \} */
