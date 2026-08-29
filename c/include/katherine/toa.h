/**
 * @file
 * @brief Timestamp units for Timepix3 pixel data.
 * @author Petr Mánek
 * @date 29.8.26
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>
#include <katherine/config.h>
#include <katherine/global.h>

/**
 * @addtogroup c_api
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

KATHERINE_EXPORTED uint8_t
katherine_tpx3_toa_coarse_tick_to_fine_ticks(katherine_freq_t freq);

KATHERINE_EXPORTED uint8_t
katherine_tpx3_toa_coarse_tick_to_fine_shift(katherine_freq_t freq);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */
