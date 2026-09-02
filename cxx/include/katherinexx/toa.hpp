/**
 * \file
 * \brief C++ wrapper for timestamp units of Timepix3 pixel data.
 * \author Petr Mánek
 * \date 30.8.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>

#include <katherine/toa.h>

#include <katherinexx/config.hpp>

namespace katherine {

/**
 * \addtogroup cxx_api
 * \{
 */

/** katherine_tpx3_timestamp_to_seconds() split into whole seconds and the fractional remainder. */
struct timestamp_seconds {
    std::uint64_t sec;
    double nsec;
};

/** katherine_tpx3_timestamp_to_toa_ftoa() recovered chip counters: coarse ticks since the epoch and the fine remainder. */
struct toa_ftoa {
    std::uint64_t toa;
    std::uint8_t ftoa;
};

/**
 * Fine-oscillator ticks in one pixel-clock tick.
 * \param freq Pixel-clock frequency selector.
 * \return Fine ticks per coarse tick.
 */
static inline std::uint8_t
tpx3_toa_coarse_tick_to_fine_ticks(katherine::freq freq)
{
    return katherine_tpx3_toa_coarse_tick_to_fine_ticks((katherine_freq_t) freq);
}

/**
 * The same ratio as a shift.
 * \param freq Pixel-clock frequency selector.
 * \return Shift taking coarse ticks to fine ticks.
 */
static inline std::uint8_t
tpx3_toa_coarse_tick_to_fine_shift(katherine::freq freq)
{
    return katherine_tpx3_toa_coarse_tick_to_fine_shift((katherine_freq_t) freq);
}

/**
 * The bias katherine::base_acquisition puts on the timestamp epoch.
 * \param coarse_tick_to_fine_shift Shift for the acquisition's pixel clock.
 * \return Bias in fine-oscillator ticks.
 */
static inline std::uint64_t
tpx3_toa_epoch_bias(std::uint8_t coarse_tick_to_fine_shift)
{
    return katherine_tpx3_toa_epoch_bias(coarse_tick_to_fine_shift);
}

/**
 * Convert a timestamp to whole seconds and the remainder within that second.
 * \param timestamp Timestamp in fine-oscillator ticks.
 * \return Whole seconds and the remainder within that second, in nanoseconds.
 */
static inline katherine::timestamp_seconds
tpx3_timestamp_to_seconds(std::uint64_t timestamp)
{
    katherine::timestamp_seconds result{};
    katherine_tpx3_timestamp_to_seconds(timestamp, &result.sec, &result.nsec);
    return result;
}

/**
 * Recover the chip's own time of arrival and fine time of arrival.
 * \param coarse_tick_to_fine_shift Shift for the acquisition's pixel clock.
 * \param phase_offset Phase offset applied to this pixel's column, in fine ticks.
 * \param timestamp Timestamp in fine-oscillator ticks.
 * \return Coarse ticks since the epoch (toa) and the fine ticks the hit preceded its coarse tick by (ftoa).
 */
static inline katherine::toa_ftoa
tpx3_timestamp_to_toa_ftoa(std::uint8_t coarse_tick_to_fine_shift, std::uint8_t phase_offset, std::uint64_t timestamp)
{
    katherine::toa_ftoa result{};
    katherine_tpx3_timestamp_to_toa_ftoa(coarse_tick_to_fine_shift, phase_offset, timestamp, &result.toa, &result.ftoa);
    return result;
}

/** \} */

}
